#include <iostream>
#include "navier.h"
#include <math.h>
using namespace std;

/*********************************************
 * Gradient calculation using Gaussian law
 * MPI Collective Subroutine
 * Communicate to get gradient at interface Cell
 * MUST ensure INTERFACE_COMMUNICATION is called on phi before calculate Gradient
 *********************************************/
int NavierStokesSolver::Gradient( double *phi, double *Bphif, double **phigd )
{
    	int    i,g, c1,c2;
    	double lambda,pf;
    	// using Gauss theorem
    	for( i=0; i<Ncel; i++ )
        	for(g=0;g<3;g++)
            		phigd[i][g]= 0.;


    	for( i=0; i<Nfac; i++ )
    	{
        	lambda = Face[i].lambda;
        	c1     = Face[i].cell1;
        	c2     = Face[i].cell2;

        	if( c2>=0 ){
			pf = lambda*phi[c1] + (1.-lambda)*phi[c2];
			for( g=0;g<3;g++ ){
				phigd[c1][g] += pf * Face[i].n[g];
				phigd[c2][g] -= pf * Face[i].n[g];//might add to interface cell, meaningless but no harmful
			}
		}else{
		 	pf = Bphif[Face[i].bnd]; // how to add boundary condition ?
			for( g=0;g<3;g++ )
				phigd[c1][g] += pf * Face[i].n[g];
		}
    	}

	//CHECK_ARRAY(phi,Ncel);
	//CHECK_ARRAY(ED,Ncel);

    	for( i=0; i<Ncel; i++ ){
       		for( g=0; g<3; g++ ){
       	     		phigd[i][g] /= Cell[i].vol;
		}
    	}


    	if(      limiter==0 )
	{}
	else if( limiter==1 ){
		Limiter_Barth( phi, phigd );
	}
	else if( limiter==2 ){
		Limiter_MLP  ( phi, phigd );
	}
	else if( limiter==3 ){
		dataPartition->interfaceCommunicationBegin(phigd);			
		dataPartition->interfaceCommunicationEnd();
		Limiter_WENO ( phi, phigd );
	}
	else
		ErrorStop("no such limiter choice");

	//CHECK_ARRAY(phigd[0],3*Ncel);

	dataPartition->interfaceCommunicationBegin(phigd);			
	dataPartition->interfaceCommunicationEnd();
	

	return 0;
}



/*
// Gradient calculation using Gaussian law
// div( (pv1,pv2,pv3) )
int NavierStokesSolver::Divergence( double *pv1, double *pv2, double *pv3, 
								    double *pb1, double *pb2, double *pb3, double *divg )
{
    int i,c1,c2,bnd;
    double lambda,pv1f,pv2f,pv3f,pvF;
    // using Gauss theorem
    vec_init(divg,Ncel,0.);

    for( i=0; i<Nfac; i++ )
    {
        lambda = Face[i].lambda;
        c1     = Face[i].cell1;
        c2     = Face[i].cell2;
		bnd    = Face[i].bnd;
        if( c2>=0 )
        {
            pv1f = lambda*pv1[c1] + (1.-lambda)*pv1[c2];
            pv2f = lambda*pv2[c1] + (1.-lambda)*pv2[c2];
            pv3f = lambda*pv3[c1] + (1.-lambda)*pv3[c2];
        }
		else
        {
            pv1f = pb1[bnd];
            pv2f = pb2[bnd];
            pv3f = pb3[bnd];
        }

        // add to cell
		pvF = pv1f*Face[i].n[0] + pv2f*Face[i].n[1] + pv3f*Face[i].n[2];
        divg[c1] += pvF;
        if( c2>=0 )
        divg[c2] -= pvF;
    }

    for( i=0; i<Ncel; i++ )
        divg[i] /= Cell[i].vol;
    return 0;
}
*/


/////////////////////////////////////////////////
/// two kinds of limiters : MLP, WENO
///------------------------------------------

inline double Barth_fun( double us,double umin, double umax)
{
    double ff;
    if( us>1.0e-10 )
      ff= CYCASMIN(1., umax/us );
    else if( us< -1.0e-10 )
      ff= CYCASMIN(1., umin/us );
    else
      ff= 1.;
    return ff;
}
inline double smoothfun( double us,double umin,double umax, double epsilon2)
{
	double ff,umax2,umin2,us2;
    if( us>1.0e-10){
		umax2= umax * umax;
		us2  = us   * us;
		ff= 1./(CYCASSIGN(us)*(fabs(us)+1.e-12)) *
            ( (umax2+epsilon2)*us+ 2.*us2*umax )/( umax2+2.*us2+us*umax+ epsilon2);
	}
    else if( us< -1.0e-10 ){
		umin2= umin * umin;
		us2  = us   * us  ;
		ff= 1./(CYCASSIGN(us)*(fabs(us)+1.e-12)) *
            ( (umin2+epsilon2)*us+ 2.*us2*umin )/( umin2+2.*us2+us*umin+ epsilon2);
	}
    else
      ff= 1.;
    return ff;
}


/******************************************************
 *	this limiter is refactored with MPL
 ******************************************************/
int NavierStokesSolver::Limiter_MLP(double UC[],double **GradU)
{
    int    i,j,iv;
    double *ficell, *UMax,*UMin, fi,umin,umax,dx,dy,dz,us;
    ficell = new double[Ncel];
    UMax   = new double[Nvrt];
    UMin   = new double[Nvrt];

    // vertex max and min value
    for( i=0; i<Nvrt; i++ )
    {
        UMax[i]= -1.e8;
        UMin[i]=  1.e8;
    }
    for( i=0; i<Ncel; i++ )
    {
        for(j=0; j<8; j++ ) // this may be a little wasteful, optimize later
        {
            iv = Cell[i].vertices[j];
            UMax[iv]= CYCASMAX( UMax[iv], UC[i] );
            UMin[iv]= CYCASMIN( UMin[iv], UC[i] );
        }
	
	for(j=0;j!=Cell[i].nface;++j){// this for is added for virtual interface Cell, to be optimized
		int neighbourCell = Cell[i].cell[j];
		if ( neighbourCell >= Ncel ){//for each virtual cell
			for(int k=0;k!=4;++k){
				iv = Face[ Cell[i].face[j] ].vertices[k];
				UMax[iv] = CYCASMAX( UMax[iv],UC[neighbourCell] );
				UMin[iv] = CYCASMIN( UMin[iv],UC[neighbourCell] );
			}	
		}
	}
    }


    // MLP : find slope limit coef; limit gradients
    for( i=0; i<Ncel; i++ )
    {
        fi = 10.;
        for(j=0; j<8; j++ ) // this may be a little wasteful, 8 changed to more 
        {
            iv = Cell[i].vertices[j];
            umax= UMax[iv] - UC[i];
            umin= UMin[iv] - UC[i];

            dx = Vert[iv][0] - Cell[i].x[0];
            dy = Vert[iv][1] - Cell[i].x[1];
            dz = Vert[iv][2] - Cell[i].x[2];
            us = GradU[i][0]*dx + GradU[i][1]*dy + GradU[i][2]*dz ;

	    // Barth
	    fi = CYCASMIN( fi, Barth_fun(us,umin,umax) );
	    // Venkatanishnan
	    // fi = CYCASMIN( fi, smoothfun( us,umin,umax, ? ) );
        }
        ficell[i]= fi;
    }
    //CHECK_ARRAY(UC,Ncel);
    //CHECK_ARRAY(ficell,Ncel);
    //CHECK_ARRAY(GradU[0],3*Ncel);
    for( i=0; i<Ncel; i++ )
    {
        GradU[i][0] = ficell[i] * GradU[i][0];
        GradU[i][1] = ficell[i] * GradU[i][1];
        GradU[i][2] = ficell[i] * GradU[i][2];
    }

    //CHECK_ARRAY(GradU[0],3*Ncel);

    delete [] ficell;
    delete [] UMax;
    delete [] UMin;
    return 0;
}

int NavierStokesSolver::Limiter_Barth(double UC[],double **GradU)
{
    int    i,j,iv,in;
    double *ficell, fi,umin,umax,dx,dy,dz,us;
    ficell = new double[Ncel];

    // MLP : find slope limit coef; limit gradients
    for( i=0; i<Ncel; i++ )
    {
        fi = 10.;
	umax= UC[i];
        umin= UC[i];
	for(j=0; j<Cell[i].nface; j++ ) // this may be a little wasteful, optimize later
        {
		in = Cell[i].cell[j];
		if( in<0 )continue;
            	umax= CYCASMAX( umax, UC[in] );
            	umin= CYCASMIN( umin, UC[in] );
        }
	umax -= UC[i];
        umin -= UC[i];
	for(j=0; j<8; j++ ) // this may be a little wasteful, 8 changed to more 
	{
        	iv = Cell[i].vertices[j];
		dx = Vert[iv][0] - Cell[i].x[0];
		dy = Vert[iv][1] - Cell[i].x[1];
            	dz = Vert[iv][2] - Cell[i].x[2];
            	us = GradU[i][0]*dx + GradU[i][1]*dy + GradU[i][2]*dz ;
		// Barth
		fi = CYCASMIN( fi, Barth_fun(us,umin,umax) );
        }
        ficell[i]= fi;
    }
    for( i=0; i<Ncel; i++ )
    {
        GradU[i][0] = ficell[i] * GradU[i][0];
        GradU[i][1] = ficell[i] * GradU[i][1];
        GradU[i][2] = ficell[i] * GradU[i][2];
    }

    delete [] ficell;
    return 1;
}

int NavierStokesSolver::Limiter_WENO(double *UC,double **GradU)
{
    int i,j, ic,iface;
    double ss,wei, **gdTmp;

    gdTmp = new_Array2D<double>( Ncel, 3 );
	for( i=0; i<Ncel; i++ )
	{
		gdTmp[i][0]= 0.;
		gdTmp[i][1]= 0.;
		gdTmp[i][2]= 0.;
	}

    // WENO Limiter
    for( i=0; i<Ncel; i++ )
    {
        // non-linear weights
        ss = 0.;
        for( j=0; j<Cell[i].nface; j++ )
        {
            iface = Cell[i].face[j];
            ic    = Cell[i].cell[j];
            if( ic<0 ) continue;
            wei = 1./( GradU[ic][0]*GradU[ic][0] +
                       GradU[ic][1]*GradU[ic][1] +
                       GradU[ic][2]*GradU[ic][2] +1.e-16 );
            ss += wei;
	    gdTmp[i][0] += wei*GradU[ic][0];
	    gdTmp[i][1] += wei*GradU[ic][1];
	    gdTmp[i][2] += wei*GradU[ic][2];
        }

		//// boundary cells, just quit
		//if(j<Cell[i].nface)
		//{
		//	gdTmp[i][0] = GradU[i][0];
		//	gdTmp[i][1] = GradU[i][1];
		//	gdTmp[i][2] = GradU[i][2];
		//	continue;
		//}

        wei     = 1./( GradU[i ][0]*GradU[i ][0] +
                       GradU[i ][1]*GradU[i ][1] +
                       GradU[i ][2]*GradU[i ][2] +1.e-16 );
        ss += wei;

	gdTmp[i][0] += wei*GradU[i][0];
	gdTmp[i][1] += wei*GradU[i][1];
	gdTmp[i][2] += wei*GradU[i][2];
	gdTmp[i][0] /= ss;
	gdTmp[i][1] /= ss;
	gdTmp[i][2] /= ss;
	
    }
    // substitution
    for( i=0; i<Ncel; i++ )
    {
        GradU[i][0]= gdTmp[i][0];
        GradU[i][1]= gdTmp[i][1];
        GradU[i][2]= gdTmp[i][2];
    }

    delete_Array2D( gdTmp, Ncel, 3 );
    return 1;
}

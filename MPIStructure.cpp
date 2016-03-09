
#include "MPIStructure.h"

using namespace std;


int DataPartition::initPetsc(){ //collcetive

	MPI_Barrier(comm);

	PetscPrintf(comm,"PETSC initing\n");
		
	//init PETSC vec
	ierr = VecCreateMPI(comm,nLocal,nGlobal,&bu);CHKERRQ(ierr); 
	ierr = VecDuplicate(bu,&bv);CHKERRQ(ierr);
	ierr = VecDuplicate(bu,&bw);CHKERRQ(ierr);
	ierr = VecDuplicate(bu,&bp);CHKERRQ(ierr);
	ierr = VecDuplicate(bu,&bs);CHKERRQ(ierr);

	ierr = VecSet(bu,0.0);CHKERRQ(ierr);
	ierr = VecSet(bv,0.0);CHKERRQ(ierr);
	ierr = VecSet(bw,0.0);CHKERRQ(ierr);
	ierr = VecSet(bp,0.0);CHKERRQ(ierr);
	ierr = VecSet(bs,0.0);CHKERRQ(ierr);

	//init PETSC mat
	ierr = MatCreate(comm,&Au);CHKERRQ(ierr);     
	ierr = MatSetSizes(Au,nLocal,nLocal,nGlobal,nGlobal);CHKERRQ(ierr);
	ierr = MatSetType(Au,MATAIJ);CHKERRQ(ierr);
	ierr = MatMPIAIJSetPreallocation(Au,MAX_LOCAL_PREALLOCATION,NULL,MAX_LOCAL_PREALLOCATION,NULL);CHKERRQ(ierr);	


	ierr = MatCreate(comm,&Ap);CHKERRQ(ierr);     
	ierr = MatSetSizes(Ap,nLocal,nLocal,nGlobal,nGlobal);CHKERRQ(ierr);
	ierr = MatSetType(Ap,MATAIJ);CHKERRQ(ierr);
	ierr = MatMPIAIJSetPreallocation(Ap,MAX_LOCAL_PREALLOCATION,NULL,MAX_LOCAL_PREALLOCATION,NULL);CHKERRQ(ierr);	


	ierr = MatCreate(comm,&As);CHKERRQ(ierr);     
	ierr = MatSetSizes(As,nLocal,nLocal,nGlobal,nGlobal);CHKERRQ(ierr);
	ierr = MatSetType(As,MATAIJ);CHKERRQ(ierr);
	ierr = MatMPIAIJSetPreallocation(As,MAX_LOCAL_PREALLOCATION,NULL,MAX_LOCAL_PREALLOCATION,NULL);CHKERRQ(ierr);	

	//init KSP context
	ierr = KSPCreate(comm,&ksp);

	printf("dataPartition PETSC NO. %d init complete, dimension %d x %d = %d\n",comRank,nLocal,nProcess,nGlobal);

	return 0;
}


int DataPartition::deinit(){
	printf("datagroup NO. %d died\n",comRank);
	ierr = VecDestroy(&bu);CHKERRQ(ierr);
	ierr = VecDestroy(&bv);CHKERRQ(ierr);
	ierr = VecDestroy(&bw);CHKERRQ(ierr);
	ierr = VecDestroy(&bp);CHKERRQ(ierr);
	ierr = VecDestroy(&bs);CHKERRQ(ierr);
	ierr = MatDestroy(&Au);CHKERRQ(ierr);
	ierr = MatDestroy(&Ap);CHKERRQ(ierr);
	ierr = MatDestroy(&As);CHKERRQ(ierr);
	ierr = KSPDestroy(&ksp);CHKERRQ(ierr);
	delete []gridList;
	gridList=NULL;
	return 0;
};




/*****************************************************
 *	MPI ScatterV / Send routine
 *	scatter geometry data to each partition
 *****************************************************/
int DataPartition::fetchDataFrom(RootProcess& root){ //collective 
	int sourceRank=root.rank;

	int destCount=0;
	int* sourceCount = new int[nProcess];
	int* offsets = new int[nProcess];
	double* localArray = NULL; 

	MPI_Barrier(comm);

	printf("start fetching data from root\n");
	offsets[0] = 0;
	sourceCount[0] = gridList[0];
 	//*************************fetching vecotr U***************************	
	for(int i=1;i!=nProcess;++i){
		sourceCount[i] = gridList[i];
		offsets[i] = offsets[i-1] + sourceCount[i];
	}
	destCount = nLocal;
	ierr = VecGetArray(bu,&localArray);CHKERRQ(mpiErr); //fetch raw pointer of PetscVector;

	//mpiErr = MPI_Scatterv(root.rootuBuffer,sourceCount,offsets,MPI_DOUBLE,
	//		localArray,destCount,MPI_DOUBLE,sourceRank,comm); CHECK(mpiErr)
	/*
	if(comRank==root.rank)
	for(int i=0;i!=nLocal;++i)	
		localArray[i] = root.rootuBuffer[i];
	*/	

	ierr = VecRestoreArray(bu,&localArray);CHKERRQ(mpiErr);

 	//*************************fetching Matrix A***************************	


	delete offsets;
	delete sourceCount;
	printf("complete fetching data from root\n");
	return 0;
}

int DataPartition::pushDataTo(RootProcess& root){//collective reverse progress of the fetch function
	MPI_Barrier(comm);
	printf("begin pushing data to root\n");

	root.allocate(this);
	pushVectorToRoot(bu,root.rootArrayBuffer,root.rank);

	printf("complete pushing data to root\n");
	return 0;
}

//********last 2 parameter only significant at root, sending PETSC vector only!***********
//			collective call
//************************************************************************
int DataPartition::pushVectorToRoot(const Vec& petscVec,double* rootBuffer,int rootRank){
	double* sendbuf = NULL;
	int sendcount = 0;

	double* recvbuf = NULL;
	int* recvcount = NULL; // significant only at root
	int* offsets = NULL;    // significant only at root
	
	recvcount = new int[nProcess];
	offsets = new int[nProcess];
	offsets[0] = 0;
 	//*************************pushing vecotr U***************************	
	recvcount[0] = gridList[0];
	for(int i=1;i!=nProcess;++i){
		recvcount[i] = gridList[i];
		offsets[i] = offsets[i-1] + recvcount[i];
	}
	sendcount = nLocal;
	recvbuf = rootBuffer;
	ierr = VecGetArray(petscVec,&sendbuf);CHKERRQ(ierr);
	mpiErr = MPI_Gatherv( sendbuf,sendcount,MPI_DOUBLE,
			      recvbuf,recvcount,offsets,MPI_DOUBLE,
			      rootRank,comm
			    );CHECK(mpiErr)

	ierr = VecRestoreArray(petscVec,&sendbuf);CHKERRQ(ierr);


	delete recvcount;
	delete offsets;

	return 0;

}

int DataPartition::buildMatrix(){ //local but should involked in each processes
	PetscInt linecounter = 0;
	int ibegin=0, iend=0;
	PetscInt iInsert=0;
	PetscInt* jInsert = new PetscInt[MAX_ROW]; // it is necesasry to prescribe the max index of a row
	PetscScalar* vInsert = new PetscScalar[MAX_ROW];

	ierr = MatGetOwnershipRange(Au,&ibegin,&iend);CHKERRQ(ierr);//get range in global index
	for(int i=0;i!=nLocal;++i){ //this loop should be optimized with local parallel, tbb , openMP, etc.
		linecounter = 0;
		for(int j=0;j!=MAX_ROW;++j){
			//matrix building!
			linecounter++;
		}
		iInsert = ibegin + i;
		ierr = MatSetValues(Au,1,&iInsert,linecounter,jInsert,vInsert,INSERT_VALUES);CHKERRQ(ierr); //build matrix line by line
	}

	ierr = MatAssemblyBegin(Au,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
	//!!!!!!!!!!!!!!!!!!!!!!END ASSEMBLY IS AT SOLVE GMRES!!!!!!!!!!!!!//

	printf("process%d complete buildMatrix\n",comRank);

	delete jInsert;
	delete vInsert;
	return 0;
}


int DataPartition::solveGMRES(double tol, int maxIter){
	KSPConvergedReason reason;
	int iters;
	ierr = MatAssemblyEnd(Au,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
	MPI_Barrier(comm);
	
	KSPSetOperators(ksp,Au,Au);
	KSPSetType(ksp,KSPGMRES);
	KSPSetInitialGuessNonzero(ksp,PETSC_TRUE);

	/***************************************
	 *      SET  TOLERENCE
	 ***************************************/
	KSPSetTolerances(ksp,tol,PETSC_DEFAULT,PETSC_DEFAULT,maxIter);	


	/***************************************
	 * 	ILU preconditioner:
	 ***************************************/
	//KSPGetPC(ksp,&pc);
	KSPSetFromOptions(ksp);
	KSPSetUp(ksp); //the precondition is done at this step


	/***************************************
	 * 	SOLVE!
	 ***************************************/
	//KSPSolve(ksp,bu,u);
	//KSPView(ksp,PETSC_VIEWER_STDOUT_WORLD);
	KSPGetConvergedReason(ksp,&reason);
	if(reason<0){
		printf("seems the KSP didnt converge :(\n");
		return 1;
	}else if(reason ==0){
		printf("why is this program still running?\n");
	}else{
		KSPGetIterationNumber(ksp,&iters);
		printf("KSP converged in %d step! :)\n",iters);
		return 0;
	}
	



	return 0;	
}



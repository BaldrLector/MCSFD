#include "TypeStVKMaterial.h"
#include <complex>
#include <omp.h>
#include "Functions/LoboMacros.h"
#include "MCSFD/MatrixOp.h"

LOBO_TEMPLATE_INSTANT(TypeStVKMaterial)

template <class TYPE>
TypeStVKMaterial<TYPE>::TypeStVKMaterial(Lobo::LoboTetMesh *tetmesh, int enableCompressionResistance /*= 0*/, TYPE compressionResistance /*= 0.0*/) : TypeTetElementMaterial<TYPE>(tetmesh, enableCompressionResistance, compressionResistance)
{
}

template <class TYPE>
TypeStVKMaterial<TYPE>::~TypeStVKMaterial()
{
}

#define ENERGY_FUNCTION(LoboComplex_NAME)                                            \
	E_complex = F_complex.transpose() * F_complex;                                   \
	E_complex.data[0] -= 1;                                                          \
	E_complex.data[4] -= 1;                                                          \
	E_complex.data[8] -= 1;                                                          \
	E_complex = E_complex * (TYPE)0.5;                                               \
	energy = (TYPE)muLame[elementIndex] * lobo::inner_product(E_complex, E_complex); \
	LoboComplex_NAME E_trace = E_complex.trace();                                    \
	E_trace = (TYPE)0.5 * lambdaLame[elementIndex] * E_trace * E_trace;              \
	energy += E_trace;

template <class TYPE>
TYPE TypeStVKMaterial<TYPE>::ComputeEnergy(int elementIndex, TYPE *invariants)
{
	TYPE IC = invariants[0];
	TYPE IIC = invariants[1];
	//double IIIC = invariants[2]; // not needed for StVK

	TYPE energy = 0.125 * lambdaLame[elementIndex] * (IC - 3.0) * (IC - 3.0) + 0.25 * muLame[elementIndex] * (IIC - 2.0 * IC + 3.0);

	this->AddCompressionResistanceEnergy(elementIndex, invariants, &energy);

	return energy;
}

template <class TYPE>
void TypeStVKMaterial<TYPE>::ComputeEnergyGradient(int elementIndex, TYPE *invariants, TYPE *gradient)
{
	TYPE IC = invariants[0];
	gradient[0] = 0.25 * lambdaLame[elementIndex] * (IC - 3.0) - 0.5 * muLame[elementIndex];
	gradient[1] = 0.25 * muLame[elementIndex];
	gradient[2] = 0.0;

	this->AddCompressionResistanceGradient(elementIndex, invariants, gradient);
}

template <class TYPE>
void TypeStVKMaterial<TYPE>::ComputeEnergyHessian(int elementIndex, TYPE *invariants, TYPE *hessian)
{
	hessian[0] = 0.25 * lambdaLame[elementIndex];
	// 12
	hessian[1] = 0.0;
	// 13
	hessian[2] = 0.0;
	// 22
	hessian[3] = 0.0;
	// 23
	hessian[4] = 0.0;
	// 33
	hessian[5] = 0.0;

	this->AddCompressionResistanceHessian(elementIndex, invariants, hessian);
}




template <class TYPE>
void TypeStVKMaterial<TYPE>::computeImaginaryEnergyVector(int elementIndex, TYPE *forces, Matrix3 &F, TYPE h)
{
	// 0
	//try
	TYPE e = 0;
	TYPE second_e = 0;
	for (int n = 0; n < 9; n++)
	{
		int r = n % 3;
		int c = n / 3;
		e = 0;

		Matrix3 E = 0.5 * (F.transpose() * F - Matrix3::Identity() + FC_FC[elementIndex * 9 + n]);

		Matrix3 P = F * (2 * muLame[elementIndex] * E + lambdaLame[elementIndex] * E.trace() * Matrix3::Identity());
		for (int j = 0; j < 9; j++)
		{
			e += P.data()[j] * F_complex[elementIndex * 9 + n].data()[j];
		}

		forces[n] = e;
	}

	for (int i = 0; i < 3; i++)
	{
		forces[9 + i] = -(forces[i] + forces[3 + i] + forces[6 + i]);
	}

	for (int i = 0; i < 12; i++)
	{
		forces[i] /= h;
	}
}

template <class TYPE>
void TypeStVKMaterial<TYPE>::computeImaginaryP(int elementIndex, Matrix3 &F, Matrix3 &iF, Matrix3 &P, TYPE h)
{
	Matrix3 E_real = 0.5 * (F.transpose() * F - iF.transpose() * iF - Matrix3::Identity());
	Matrix3 E_comp = 0.5 * (iF.transpose() * F + F.transpose() * iF);

	P = 2.0 * muLame[elementIndex] * (F * E_comp + iF * E_real) + lambdaLame[elementIndex] * (F * E_comp.trace() * Matrix3::Identity() + iF * E_real.trace() * Matrix3::Identity());

	//test
	/*lobo::LoboComplexMatrix3<LoboComplexDualt, TYPE> F_tmp, E_complex,F_complex;
	LoboComplexDualt energy;
	for (int i = 0;i < 9;i++)
	{
		F_tmp.data[i].real_.real_ = F.data()[i];
		F_tmp.data[i].image_.real_ = iF.data()[i];
	}


	for (int i = 0;i < 9;i++)
	{
		F_complex = F_tmp;
		F_complex.data[i].real_.image_ = h;
		E_complex = F_complex.transpose()*F_complex;
		E_complex.data[0] -= 1;
		E_complex.data[4] -= 1;
		E_complex.data[8] -= 1;
		E_complex = E_complex*(TYPE)0.5;

		energy = (TYPE)muLame[elementIndex] * lobo::inner_product(E_complex, E_complex);
		LoboComplexDualt E_trace = E_complex.trace();
		E_trace = (TYPE)0.5*lambdaLame[elementIndex] * E_trace*E_trace;
		energy += E_trace;

		P.data()[i] = energy.image_.image_/h;
	}*/
}

template <class TYPE>
void TypeStVKMaterial<TYPE>::computeAutoDiffEnergyVector(int elementIndex, TYPE *forces, Matrix3 &F, TYPE &energy_)
{
	//std::cout<<";;;;;;;;;;;;;;;;;;;;;;;;;;;"<<std::endl;
	//lobo::LoboComplexMatrix3<LoboComplext, TYPE> F_complex,E_complex;
	LoboComplext energy;
	for (int r = 0; r < 9; r++)
	{
		this->F_complex_list[elementIndex] = element_dF_du[elementIndex * 12 + r];
		this->F_complex_list[elementIndex].setRealValue(F.data());
		lobo::multiMT(this->F_complex_list[elementIndex], this->F_complex_list[elementIndex], this->E_complex_list[elementIndex]);
		this->E_complex_list[elementIndex].data[0].real_ -= 1;
		this->E_complex_list[elementIndex].data[4].real_ -= 1;
		this->E_complex_list[elementIndex].data[8].real_ -= 1;
		//this->E_complex_list[elementIndex] = this->E_complex_list[elementIndex]*(TYPE)0.5;
		lobo::multiMScalar(this->E_complex_list[elementIndex], 0.5, this->E_complex_list[elementIndex]);

		energy.image_ = (TYPE)muLame[elementIndex] * lobo::inner_productJ(this->E_complex_list[elementIndex], this->E_complex_list[elementIndex]);
		//energy = (TYPE)muLame[elementIndex]*lobo::inner_product(this->E_complex_list[elementIndex], this->E_complex_list[elementIndex]);

		//LoboComplext E_trace = this->E_complex_list[elementIndex].trace();
		//E_trace = (TYPE)0.5*lambdaLame[elementIndex]*E_trace*E_trace;
		//energy += E_trace;
		energy.image_ += (TYPE)0.5 * lambdaLame[elementIndex] * lobo::traceSquareJ(this->E_complex_list[elementIndex]);

		forces[r] = energy.image_ / this->h_CSFD;
		energy_ = energy.real_;
	}

	for (int u = 0; u < 3; u++)
	{
		forces[u + 9] = -forces[0 + u] - forces[3 + u] - forces[6 + u];
	}

	/*lobo::AutoDiffMatrix3d<TYPE> E = F.Tmulti(F) - Matrix3::Identity();
	E = E * 0.5;

	lobo::AutoDiffScalar<TYPE> energy = E.FrobeniusProduct(E, true);
	energy *= muLame[elementIndex];

	lobo::AutoDiffScalar<TYPE> energy2 = E.trace(false);
	energy2 = energy2.pow(2,true);
	energy2 *= (0.5*lambdaLame[elementIndex]);
	energy += energy2;

	for (int i = 0;i < 12;i++)
	{
		forces[i] = energy.derivative.data()[i];
	}*/
}

template <class TYPE>
void TypeStVKMaterial<TYPE>::computeAutoDiffEnergyVectorDiagMatrix(int elementIndex, TYPE *forces, MatrixX *stiffness, Matrix3 &F, TYPE &energy_)
{
	stiffness->setZero();
	lobo::LoboComplexMatrix3<LoboComplexDualt, TYPE> F_complex, E_complex;
	LoboComplexDualt energy;

	for (int r = 0; r < 12; r++)
	{
		F_complex = element_dF_dudu[elementIndex * 12 + r];
		F_complex.setRealValue(F.data());

		E_complex = F_complex.transpose() * F_complex;
		E_complex.data[0] -= 1;
		E_complex.data[4] -= 1;
		E_complex.data[8] -= 1;
		E_complex = E_complex * (TYPE)0.5;

		energy = (TYPE)muLame[elementIndex] * lobo::inner_product(E_complex, E_complex);
		LoboComplexDualt E_trace = E_complex.trace();
		E_trace = (TYPE)0.5 * lambdaLame[elementIndex] * E_trace * E_trace;
		energy += E_trace;

		forces[r] = energy.real_.image_;
		stiffness->data()[r * 12 + r] = energy.image_.image_;
		energy_ = energy.real_.real_;
	}
}

template <class TYPE>
void TypeStVKMaterial<TYPE>::computeAutoDiffEnergyVectorMatrix(int elementIndex, TYPE *forces, MatrixX *stiffness, Matrix3 &F, TYPE &energy_)
{
	//new version
	this->computeAutoDiffEnergyVector(elementIndex,forces,F,energy_);
	std::vector<Matrix3> E_u(12);
	Matrix3 E_real;

	//compute stiffness
	stiffness->setZero();
	double energy;

	int index = 0;
	for (int u = 0; u < 12; u++)
	{
		for (int v = u; v < 12; v++)
		{
			int flag = lobo::MCSFD_all;
			
			if(v!=0&&u==0)
			{
				flag = lobo::MCSFD_real_image;
			}

			if (u != 0)
			{
				flag = lobo::MCSFD_image;
			}

			//flag = lobo::MCSFD_all;

			//this->F_dualcomplex_list[elementIndex] = element_dF_dudv[elementIndex * 78 + index];
			//this->F_dualcomplex_list[elementIndex].setRealValue(F.data());
			lobo::setMatrix3Value(this->F_dualcomplex_list[elementIndex],F.data(),element_dF_dudv[elementIndex * 78 + index]);
			
			if(flag==lobo::MCSFD_all)
			lobo::multiMTd_all(this->F_dualcomplex_list[elementIndex], this->F_dualcomplex_list[elementIndex], this->E_dualcomplex_list[elementIndex]);
			if(flag==lobo::MCSFD_real_image)
			lobo::multiMTd_real_image(this->F_dualcomplex_list[elementIndex], this->F_dualcomplex_list[elementIndex], this->E_dualcomplex_list[elementIndex]);
			if(flag==lobo::MCSFD_image)
			lobo::multiMTd_image(this->F_dualcomplex_list[elementIndex], this->F_dualcomplex_list[elementIndex], this->E_dualcomplex_list[elementIndex]);

			if (u == v && u == 0)
			{
				this->E_dualcomplex_list[elementIndex].data[0].real_.real_ -= 1;
				this->E_dualcomplex_list[elementIndex].data[4].real_.real_ -= 1;
				this->E_dualcomplex_list[elementIndex].data[8].real_.real_ -= 1;
			}

			lobo::multiMScalard(this->E_dualcomplex_list[elementIndex], 0.5, this->E_dualcomplex_list[elementIndex], flag);

			if(u==0)
			{
				lobo::getMatrix3Value(2,E_u[v].data(),this->E_dualcomplex_list[elementIndex]);
				if(v==0)
				{
					lobo::getMatrix3Value(0,E_real.data(),this->E_dualcomplex_list[elementIndex]);
				}
			}else
			{
				lobo::setMatrix3Value(2,this->E_dualcomplex_list[elementIndex],E_u[v].data());
				lobo::setMatrix3Value(1,this->E_dualcomplex_list[elementIndex],E_u[u].data());
				//lobo::setMatrix3Value(0,this->E_dualcomplex_list[elementIndex],E_real.data());
			}
			//std::cout<<this->E_dualcomplex_list[elementIndex].data[0].image_.real_<<" ";

			energy = (TYPE)muLame[elementIndex] * lobo::inner_productH(this->E_dualcomplex_list[elementIndex], this->E_dualcomplex_list[elementIndex]);
			energy += (TYPE)0.5 * lambdaLame[elementIndex] * lobo::traceSquareH(this->E_dualcomplex_list[elementIndex]);
			stiffness->data()[v * 12 + u] = energy / this->h_CSFD / this->h_CSFD;
			index++;
		}
		//std::cout<<std::endl;
	}

	//fill the matrix
	for (int u = 0; u < 12; u++)
	{
		for (int v = 0; v < u; v++)
		{
			stiffness->data()[v * 12 + u] = stiffness->data()[u * 12 + v];
		}
	}

	//stiffness->setZero();
	//lobo::LoboComplexMatrix3<LoboComplexDualt, TYPE> E_complex;
	// LoboComplexDualt energy;

	// int index = 0;

	// for (int u = 0;u < 12;u++)
	// {
	// 	for (int v = 0;v < u + 1;v++)
	// 	{
	// 		//std::cout << elementIndex * 78 + index <<" "<< u <<" " << v << std::endl;
	// 		//std::cout << element_dF_dudv.size() << std::endl;

	// 		this->F_dualcomplex_list[elementIndex] = element_dF_dudv[elementIndex * 78 + index];
	// 		this->F_dualcomplex_list[elementIndex].setRealValue(F.data());

	// 		/*E_complex = F_complex.transpose()*F_complex;
	// 		LoboComplexDualt I1 = E_complex.trace();
	// 		E_complex = E_complex.transpose()*E_complex;
	// 		LoboComplexDualt I2 = E_complex.trace();

	// 		energy = muLame[elementIndex] * (TYPE)0.5*(I2 - (TYPE)2.0*I1 + (TYPE)3.0);
	// 		energy += (TYPE)0.25*lambdaLame[elementIndex] * (I1 - (TYPE)3.0)*(I1 - (TYPE)3.0);*/
	// 		lobo::multiMT(this->F_dualcomplex_list[elementIndex], this->F_dualcomplex_list[elementIndex], this->E_dualcomplex_list[elementIndex]);
	// 		//this->E_dualcomplex_list[elementIndex] = this->F_dualcomplex_list[elementIndex].transpose()*this->F_dualcomplex_list[elementIndex];
	// 		this->E_dualcomplex_list[elementIndex].data[0] -= 1;
	// 		this->E_dualcomplex_list[elementIndex].data[4] -= 1;
	// 		this->E_dualcomplex_list[elementIndex].data[8] -= 1;
	// 		this->E_dualcomplex_list[elementIndex] = this->E_dualcomplex_list[elementIndex]*(TYPE)0.5;

	// 		energy = (TYPE)muLame[elementIndex] * lobo::inner_product(this->E_dualcomplex_list[elementIndex], this->E_dualcomplex_list[elementIndex]);
	// 		LoboComplexDualt E_trace = this->E_dualcomplex_list[elementIndex].trace();
	// 		E_trace = (TYPE)0.5*lambdaLame[elementIndex] * E_trace*E_trace;
	// 		energy += E_trace;

	// 		if (u == v)
	// 		{
	// 			forces[u] = energy.real_.image_/this->h_CSFD;
	// 		}

	// 		stiffness->data()[v * 12 + u] = energy.image_.image_/this->h_CSFD/this->h_CSFD;
	// 		energy_ = energy.real_.real_;
	// 		index++;
	// 	}
	// }

	// //fill the matrix
	// for (int u = 0;u < 12;u++)
	// {
	// 	for (int v = u + 1;v < 12;v++)
	// 	{
	// 		stiffness->data()[v * 12 + u] = stiffness->data()[u * 12 + v];
	// 	}
	// }
}

template <class TYPE>
void TypeStVKMaterial<TYPE>::computeDirectionalHessianMatrix(int elementIndex, TYPE *ele_u, MatrixX *stiffness, TYPE &energy_, TYPE h)
{
	stiffness->setZero();
	lobo::LoboComplexMatrix3<LoboComplexTrit, TYPE> Ds_dualcomplex, F_complex, E_complex;
	LoboComplexTrit energy;

	Lobo::TetElementData *te = tetmesh->getTetElement(elementIndex);

	Matrix3 Dm_inverse = te->Dm_inverse.cast<TYPE>();

	int index = 0;
	for (int u = 0; u < 12; u++)
	{
		for (int v = 0; v < u + 1; v++)
		{
			//F_complex_list[elementIndex].setRealValue(F.data());

			LoboComplexTrit xjj[12];
			for (int i = 0; i < 4; i++)
			{
				for (int j = 0; j < 3; j++)
				{
					xjj[i * 3 + j].setZero();
				}
			}

			xjj[u].real_.image_.real_ = h;
			xjj[v].image_.real_.real_ = h;
			for (int i = 0; i < 12; i++)
				xjj[i].real_.real_.image_ = ele_u[i] * h;

			for (int i = 0; i < 3; i++)
			{
				for (int j = 0; j < 3; j++)
				{
					Ds_dualcomplex.data[j * 3 + i] = xjj[j * 3 + i] - xjj[9 + i];
				}
			}

			Matrix3 Dm = te->Dm.cast<TYPE>();
			Ds_dualcomplex = Ds_dualcomplex + Dm.data();
			F_complex = Ds_dualcomplex * Dm_inverse.data();
			F_complex.data[0] = (TYPE)1.0;
			F_complex.data[4] = (TYPE)1.0;
			F_complex.data[8] = (TYPE)1.0;

			ENERGY_FUNCTION(LoboComplexTrit);

			stiffness->data()[v * 12 + u] = energy.image_.image_.image_;
			energy_ = energy.real_.real_.real_;

			index++;
		}
	}

	for (int u = 0; u < 12; u++)
	{
		for (int v = u + 1; v < 12; v++)
		{
			stiffness->data()[v * 12 + u] = stiffness->data()[u * 12 + v];
		}
	}
}

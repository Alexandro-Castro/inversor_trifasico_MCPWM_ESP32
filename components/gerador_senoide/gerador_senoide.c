#include <stdio.h>
#include "gerador_senoide.h"

// ---------------------------------------------------------------------------------------------------- //
void svm_run(svm_t *v){
   v->tmp1= v->Ubeta;
   v->tmp2= (0.5 * v->Ubeta) + (0.866 * v->Ualpha);
   v->tmp3= v->tmp2 - v->tmp1;

   v->VecSector=3;
   v->VecSector=(v->tmp2> 0)?(v->VecSector-1):v->VecSector;
   v->VecSector=(v->tmp3> 0)?(v->VecSector-1):v->VecSector;
   v->VecSector=(v->tmp1< 0)?(7-v->VecSector):v->VecSector;

   if(v->VecSector==1 || v->VecSector==4) {
      v->Ta = v->tmp2;
      v->Tb= v->tmp1 - v->tmp3;
      v->Tc=-v->tmp2;
   }

    else if(v->VecSector==2 || v->VecSector==5) {
      v->Ta= v->tmp3+v->tmp2;
      v->Tb= v->tmp1;
      v->Tc=-v->tmp1;
    }

    else {
      v->Ta= v->tmp3;
      v->Tb=-v->tmp3;
      v->Tc=-(v->tmp1+v->tmp2);
    }
}

#define raiz_de_3 1.73205080757

void spwm_gen(dados_modulacao_t *dados, pwm_senoidal_t *pwm, int contador){

   dados->angulo_fase = dados->ohmega * contador;

	pwm->seno = sin(dados->angulo_fase);
	pwm->cosseno = cos(dados->angulo_fase);
   //cosseno = sqrt(1 - (seno) * (seno));

	//senoide terceira harmônica:
	//seno = sin(3 * dados->ohmega * contador);

	// ondas senoidais defasadas 120º entre si
	// calcular seno = sen(theta)
	// cosseno = cos(theta) ou cosseno = sqrt(1 - (seno)**2) 
	// sen(theta - 2pi/3) = -sen(theta)/2 - sqrt(3)*cos(theta)/2
	// sen(theta - 2pi/3) = -sen(theta)/2 + sqrt(3)*cos(theta)/2

   //por algum motivo contador timer é metade do que realmente deveria ser, por tanto dividor por 4
   pwm->valor_fase_A = dados->amplitude_senoide * 0.25 * (1 + dados->indice_modulacao * pwm->seno);
   pwm->valor_fase_B = dados->amplitude_senoide * 0.25 * (1 + dados->indice_modulacao * (-pwm->seno * 0.5 - raiz_de_3 * pwm->cosseno * 0.5));
   pwm->valor_fase_C = dados->amplitude_senoide * 0.25 * (1 + dados->indice_modulacao * (-pwm->seno * 0.5 + raiz_de_3 * pwm->cosseno * 0.5));
}
// ---------------------------------------------------------------------------------------------------- //
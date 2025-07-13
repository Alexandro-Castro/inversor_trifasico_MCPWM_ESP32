#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"
#include "math.h"

typedef struct {
	float  Ualpha;          // Input: reference alpha-axis phase voltage
	float  Ubeta;           // Input: reference beta-axis phase voltage
	float  Ta;              // Output: reference phase-a switching function
	float  Tb;              // Output: reference phase-b switching function
	float  Tc;              // Output: reference phase-c switching function
	float  tmp1;            // Variable: temp variable
	float  tmp2;            // Variable: temp variable
	float  tmp3;            // Variable: temp variable
    unsigned int VecSector; // Space vector sector
} svm_t;

// ---------------------------------------------------------------------------------------------------- //
typedef struct{
    int     amplitude_senoide;      //valor de estouro do timer
    int     num_amostras;
    int     frequencia_modulacao;
    float   periodo_modulacao;
    float   freq_senoide;
    float   freq_inicial;
    float   indice_modulacao;
    float   ohmega;                 //2 . PI . f . periodo_modulacao
    float   angulo_fase;
} dados_modulacao_t;

// ---------------------------------------------------------------------------------------------------- //
typedef struct{
    float   seno;
    float   cosseno;
    //float   seno_3h;
    int     valor_fase_A;
    int     valor_fase_B;
    int     valor_fase_C;
} pwm_senoidal_t;

// ---------------------------------------------------------------------------------------------------- //

void svm_run(svm_t *v);
void spwm_gen(dados_modulacao_t *dados,  pwm_senoidal_t *pwm, int contador);

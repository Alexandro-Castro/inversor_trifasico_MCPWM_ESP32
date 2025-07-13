#include <stdio.h>
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "soc/mcpwm_reg.h"
#include "soc/mcpwm_struct.h"
#include "driver/mcpwm_prelude.h"
#include "fastmath.h"
#include "esp_timer.h"
#include "HD44780.h"
#include "gerador_senoide.h"
#include "spi_flash_mmap.h"
#include "esp_adc/adc_oneshot.h"

static const char *TAG = "projeto PWM senoidal trifasico";

/*
==================================================================================================================================
												VARIÁVEIS GLOBAIS
==================================================================================================================================
*/

// braços do inversor
const int fase_A_H = 32; // G32
const int fase_A_L = 33; // G33
const int fase_B_H = 14; // G14
const int fase_B_L = 13; // G13
const int fase_C_H = 12; // G12
const int fase_C_L = 10; // SD3

#define botao_subir GPIO_NUM_15
#define botao_descer GPIO_NUM_16
#define botao_esquerda GPIO_NUM_17
#define botao_direita GPIO_NUM_18
#define botao_confirma GPIO_NUM_19
#define botao_cancela GPIO_NUM_23
#define botao_incrmt GPIO_NUM_2
#define botao_decrmt GPIO_NUM_4

const uint8_t botoes[8] = {botao_subir, botao_descer, botao_esquerda, botao_direita, botao_confirma, botao_cancela, botao_incrmt, botao_decrmt};
uint8_t botao_lido;

#define freq_resolution_mcpwm 80E6 // frequência de 80Mhz do clock que vai para o mcpwm
#define pi 3.1415926536

#define LCD_ADDR 0x27
#define LCD_ROWS 2
#define LCD_COLS 16
#define PIN_SCL 22
#define PIN_SDA 21

#define EXAMPLE_ADC1_CHAN0 ADC_CHANNEL_0

adc_oneshot_unit_handle_t adc1_handle;
adc_oneshot_unit_init_cfg_t init_config1 = {
	.unit_id = ADC_UNIT_1,
};

typedef struct{
	bool status_modulacao;	//true ativa a modulação vetorial espacial
	bool status_contrl_pot;
	bool status_contrl_discreto;
	bool status_contrl_partd_suave;
	bool status_timer_mcpwm;
	bool status_ativar_mcpwm;
} status_t;

typedef struct{
	int dado_cad;
	float freq_cad;

	char string_num[10];
} dados_cad_t;

svm_t space_vector;
pwm_senoidal_t spwm;
dados_modulacao_t dados_pwm;
dados_cad_t dados_cad;
status_t mystatus;

//struct de configuração do timer do MCPWM
mcpwm_timer_handle_t timer = NULL;

/*
==================================================================================================================================
												FUNÇÃO DE INTERRUPÇÃO
==================================================================================================================================
*/

void IRAM_ATTR mcpwm_isr_timer(void *arg)
{	
	static int cont_amostras = 0;
	if (mystatus.status_modulacao){
		space_vector.Ualpha = sin(dados_pwm.ohmega * cont_amostras);
		space_vector.Ubeta = cos(dados_pwm.ohmega * cont_amostras);

		svm_run(&space_vector);

		space_vector.Ta = dados_pwm.amplitude_senoide * (1 + dados_pwm.indice_modulacao * space_vector.Ta) * 0.25;
		space_vector.Tb = dados_pwm.amplitude_senoide * (1 + dados_pwm.indice_modulacao * space_vector.Tb) * 0.25;
		space_vector.Tc = dados_pwm.amplitude_senoide * (1 + dados_pwm.indice_modulacao * space_vector.Tc) * 0.25;

		WRITE_PERI_REG(0x3FF5E040, space_vector.Ta);
		WRITE_PERI_REG(0x3FF5E078, space_vector.Tb);
		WRITE_PERI_REG(0x3FF5E0B0, space_vector.Tc);
	}
	else{
		spwm_gen(&dados_pwm, &spwm, cont_amostras);

		WRITE_PERI_REG(0x3FF5E040, spwm.valor_fase_A);
		WRITE_PERI_REG(0x3FF5E078, spwm.valor_fase_B);
		WRITE_PERI_REG(0x3FF5E0B0, spwm.valor_fase_C);
	}
	cont_amostras += 1;
	if (cont_amostras >= dados_pwm.num_amostras){
		cont_amostras = 0;
	}
}

/*
==================================================================================================================================
												FUNÇÕES UTILIZADAS
==================================================================================================================================
*/

void inicia_mcpwm();
void ler_botoes(void *pvParameter);
void menu(void *pvParameter);
void task1(void *pvParameter);
void conversao_AD(void *pvParameter);
void controle_de_freq(void *pvParameter);
void partida_suave(void *pvParameter);

/*
==================================================================================================================================
									CONFIGURAÇÃO DO PROGRAMA E LOOP PRINCIPAL
==================================================================================================================================
*/

void app_main(void) // configurações iniciais
{
	// configurações do pwm e da geração da senóide
	//--------------------------------------------------------------------------------------------------------------
	dados_pwm.frequencia_modulacao = 20000; // frequéncia de 20kHz para a onda triangular gerada pelos timers
	dados_pwm.periodo_modulacao = 50E-6;
	dados_pwm.freq_senoide = 60.0;
	dados_pwm.freq_inicial = 6.0;
	dados_pwm.indice_modulacao = 0.1;
	// valor da amplitude da onda senoidal (mesmo do valor máx do período do timer da triangular)
	dados_pwm.amplitude_senoide = freq_resolution_mcpwm / dados_pwm.frequencia_modulacao;
	dados_pwm.ohmega = 2 * pi * dados_pwm.freq_inicial * dados_pwm.periodo_modulacao;
	dados_pwm.num_amostras = dados_pwm.frequencia_modulacao / dados_pwm.freq_inicial;

	mystatus.status_modulacao = true;
	mystatus.status_contrl_pot = false;
	mystatus.status_contrl_discreto = false;
	mystatus.status_contrl_partd_suave = false;
	mystatus.status_timer_mcpwm = false;

	//--------------------------------------------------------------------------------------------------------------

	lcd_init();
	lcd_clear();
	lcd_set_cursor(0, 0);
	lcd_send_string("lcd i2c ok");
	// vTaskDelay(1000 / portTICK_PERIOD_MS);

	//--------------------------------------------------------------------------------------------------------------

	// configuração conversão analógica digital

	ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

	adc_oneshot_chan_cfg_t config = {
		.bitwidth = ADC_BITWIDTH_DEFAULT,
		.atten = ADC_ATTEN_DB_12,
	};
	ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, EXAMPLE_ADC1_CHAN0, &config));

	//--------------------------------------------------------------------------------------------------------------
	esp_rom_gpio_pad_select_gpio(GPIO_NUM_9);
	gpio_set_direction(GPIO_NUM_9, GPIO_MODE_OUTPUT);
	esp_rom_gpio_pad_select_gpio(GPIO_NUM_25);
	gpio_set_direction(GPIO_NUM_25, GPIO_MODE_OUTPUT);
	//--------------------------------------------------------------------------------------------------------------

	xTaskCreate(task1, "task1", 1024, NULL, 5, NULL);
	xTaskCreatePinnedToCore(ler_botoes, "ler_botoes", 2048, NULL, 4, NULL, 1);
	xTaskCreatePinnedToCore(menu, "menu", 2048, NULL, 4, NULL, 1);
	xTaskCreatePinnedToCore(conversao_AD, "conversao_AD", 2048, NULL, 4, NULL, 1);
	xTaskCreatePinnedToCore(controle_de_freq, "controle_de_freq", 2048, NULL, 4, NULL, 1);
	xTaskCreatePinnedToCore(partida_suave, "partida_suave", 2048, NULL, 4, NULL, 1);

	//--------------------------------------------------------------------------------------------------------------

	while (1){
		// MCPWM foi ativado no menu
		if(mystatus.status_timer_mcpwm){
			inicia_mcpwm();

			mystatus.status_timer_mcpwm = false;
		}
		vTaskDelay(500 / portTICK_PERIOD_MS);
	}
} // end main

/*
==================================================================================================================================
												  FUNÇÕES UTILIZADAS
==================================================================================================================================
*/

void conversao_AD(void *pvParameters) {
    // Tamanho da janela de média móvel
    static const int tamanho_janela = 50;
    static uint16_t dados_cad_FMM[50] = {0};
    static float soma_valores = 0.0;
    static int posicao_atu = 0;
    static bool janela_cheia = false;

    while (1) {
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, EXAMPLE_ADC1_CHAN0, &dados_cad.dado_cad));

        // Adicionar o novo valor à soma
        soma_valores += dados_cad.dado_cad;

        // Se a janela estiver cheia, subtrair o valor mais antigo
        if (janela_cheia) {
            soma_valores -= dados_cad_FMM[posicao_atu];
        }

        // Armazenar o novo valor no buffer de média móvel
        dados_cad_FMM[posicao_atu] = dados_cad.dado_cad;

        // Calcular a média móvel
        float media_movel = soma_valores / (janela_cheia ? tamanho_janela : (posicao_atu + 1));

        // Atualizar a frequência com base na média móvel
        dados_cad.freq_cad = (float)media_movel * 114 * 0.000244200244 + 6;

        // Atualizar a posição na janela
        posicao_atu = (posicao_atu + 1) % tamanho_janela;

        // Verificar se a janela está cheia
        if (posicao_atu == 0) {
            janela_cheia = true;
        }

        // Converter a frequência para string
        sprintf(dados_cad.string_num, "%.2f", dados_cad.freq_cad);

        // Delay para a próxima leitura
        vTaskDelay(150 / portTICK_PERIOD_MS);
    }
}


void controle_de_freq(void *pvParameters){
	char string_indice_mod[10];
	while (1)
	{
		while (mystatus.status_contrl_pot == true && mystatus.status_contrl_partd_suave == false)
		{
			dados_pwm.ohmega = 2 * pi * dados_cad.freq_cad * dados_pwm.periodo_modulacao;
			dados_pwm.num_amostras = (int)dados_pwm.frequencia_modulacao / dados_cad.freq_cad;

			// se a frequência for menor que a frequência nominal da senoidal
			// ajustar o índice de modulação
			if (dados_cad.freq_cad <= dados_pwm.freq_senoide){
				dados_pwm.indice_modulacao = dados_cad.freq_cad / dados_pwm.freq_senoide;

				lcd_set_cursor(1, 0);
				lcd_send_string("tensao:        ");
				sprintf(string_indice_mod, "%.2f", dados_pwm.indice_modulacao);
				lcd_set_cursor(1, 7);
				lcd_send_string(string_indice_mod);
			}
			else{
				dados_pwm.indice_modulacao = 0.9;
				lcd_set_cursor(1, 0);
				lcd_send_string("tensao:        ");
				sprintf(string_indice_mod, "%.2f", dados_pwm.indice_modulacao);
				lcd_set_cursor(1, 7);
				lcd_send_string(string_indice_mod);
			}

			//lcd_clear();
			lcd_set_cursor(0, 0);
			lcd_send_string("Freq:           ");
			lcd_set_cursor(0, 6);
			lcd_send_string(dados_cad.string_num);
			vTaskDelay(200 / portTICK_PERIOD_MS);
		}

		while (mystatus.status_contrl_discreto == true && mystatus.status_contrl_partd_suave == false){
			lcd_set_cursor(0, 0);
			lcd_send_string("nada para fazer ");
			lcd_set_cursor(1, 0);
			lcd_send_string("nada para fazer ");

			vTaskDelay(200 / portTICK_PERIOD_MS);
		}
		vTaskDelay(600 / portTICK_PERIOD_MS);
	}
}


void partida_suave(void *pvParameters){
	float freq_ref = 60.0;
	static float freq_inicial = 6.0;
	char string_indice_mod[10];
	char string_freq[10];

	while (1){
		while(mystatus.status_contrl_partd_suave == true && mystatus.status_timer_mcpwm == false){
			if(mystatus.status_contrl_pot){
				freq_ref = dados_cad.freq_cad;
			}
			else{
				// valor da frequência selecionado através dos botôes
				freq_ref = 60.0;
			}
			
			dados_pwm.ohmega = 2 * pi * freq_inicial * dados_pwm.periodo_modulacao;
			dados_pwm.num_amostras = (int)dados_pwm.frequencia_modulacao / freq_inicial;

			// se a frequência for menor que a frequência nominal da senoidal
			// ajustar o índice de modulação
			if (freq_inicial <= dados_pwm.freq_senoide){
				dados_pwm.indice_modulacao = freq_inicial / dados_pwm.freq_senoide;

				lcd_set_cursor(1, 0);
				lcd_send_string("tensao:        ");
				sprintf(string_indice_mod, "%.2f", dados_pwm.indice_modulacao);
				lcd_set_cursor(1, 7);
				lcd_send_string(string_indice_mod);
			}
			else{
				dados_pwm.indice_modulacao = 0.9;
				lcd_set_cursor(1, 0);
				lcd_send_string("tensao:        ");
				sprintf(string_indice_mod, "%.2f", dados_pwm.indice_modulacao);
				lcd_set_cursor(1, 7);
				lcd_send_string(string_indice_mod);
			}

			sprintf(string_freq, "%.2f", freq_inicial);
			lcd_set_cursor(0, 0);
			lcd_send_string("Freq:           ");
			lcd_set_cursor(0, 6);
			lcd_send_string(string_freq);


			freq_inicial += 1.0;

			if(freq_inicial >= freq_ref){
				mystatus.status_contrl_partd_suave = false;				
			}

			vTaskDelay(pdMS_TO_TICKS(100));
		}
		vTaskDelay(pdMS_TO_TICKS(600));	
	} 
}


void menu(void *pvParameter){
	// static bool status = true;
	static bool stts_bot_esq, stts_bot_drt, stts_bot_cm, stts_bot_bx;
	static bool status_menu_aux1 = true;
	static bool status_menu_aux2;

	lcd_clear();
	lcd_set_cursor(0, 0);
	lcd_send_string("menu ok");
	vTaskDelay(1000 / portTICK_PERIOD_MS);

	while (1)
	{
		if (status_menu_aux1)
		{
			lcd_clear();
			lcd_set_cursor(0, 0);
			lcd_send_string("modulacao:      ");
			lcd_set_cursor(1, 0);
			lcd_send_string("SPWM<        SVM");

			stts_bot_esq = true;
			while (status_menu_aux1)
			{
				if (botao_lido == botao_direita)
				{
					botao_lido = 0;
					stts_bot_drt = true;
					stts_bot_esq = false;

					lcd_set_cursor(1, 4);
					lcd_send_string(" ");
					lcd_set_cursor(1, 12);
					lcd_send_string(">");
				}
				else if (botao_lido == botao_esquerda) // botao esquerdo pressionado
				{
					botao_lido = 0;
					stts_bot_esq = true;
					stts_bot_drt = false; // desativando status do botao direito

					lcd_set_cursor(1, 4);
					lcd_send_string("<");
					lcd_set_cursor(1, 12);
					lcd_send_string(" ");
				}

				// testando quais botoões foram apertados para tomar a decisão do modulador do PWM
				if (botao_lido == botao_confirma && stts_bot_drt == true)
				{
					// SVM foi selecionado como modulador do pwm

					botao_lido = 0;
					mystatus.status_modulacao = true; // indica que será usada SVM
					status_menu_aux1 = false;
					status_menu_aux2 = true;

					lcd_clear();
					lcd_set_cursor(0, 0);
					lcd_send_string("SVM selecionado");
					vTaskDelay(1000 / portTICK_PERIOD_MS);
				}
				else if (botao_lido == botao_confirma && stts_bot_esq == true)
				{
					// SPWM foi selecionado como modulador do pwm

					botao_lido = 0;
					mystatus.status_modulacao = false; // indica que será usado SPWM
					status_menu_aux1 = false;
					status_menu_aux2 = true;

					lcd_clear();
					lcd_set_cursor(0, 0);
					lcd_send_string("SPWM selecionado");
					vTaskDelay(1000 / portTICK_PERIOD_MS);
				}

				vTaskDelay(400 / portTICK_PERIOD_MS);
			} // end while(status_menu_aux1)
		} // end if(status_menu_aux1)	-- parte 1 do menu finalizado

		if (status_menu_aux2)
		{
			lcd_clear();
			lcd_set_cursor(0, 0);
			lcd_send_string(">analogico      ");
			lcd_set_cursor(1, 0);
			lcd_send_string(" discreto  ");

			botao_lido = 0;
			stts_bot_cm = true;
			while (status_menu_aux2)
			{
				vTaskDelay(400 / portTICK_PERIOD_MS);

				if (botao_lido == botao_descer)
				{
					botao_lido = 0;
					stts_bot_cm = false;
					stts_bot_bx = true;

					lcd_set_cursor(0, 0);
					lcd_send_string(" ");
					lcd_set_cursor(1, 0);
					lcd_send_string(">");
				}
				else if (botao_lido == botao_subir)
				{
					botao_lido = 0;
					stts_bot_cm = true;
					stts_bot_bx = false;

					lcd_set_cursor(0, 0);
					lcd_send_string(">");
					lcd_set_cursor(1, 0);
					lcd_send_string(" ");
				} // end else if

				if (botao_lido == botao_confirma && stts_bot_cm == true)
				{

					lcd_clear();
					lcd_set_cursor(0, 0);
					lcd_send_string("iniciando   ");
					lcd_set_cursor(0, 0);
					lcd_send_string("partida suave");

					// controle de frequência pelo potenciômetro ativado
					status_menu_aux2 = false;
					botao_lido = 0;
					vTaskDelay(500 / portTICK_PERIOD_MS);

					mystatus.status_timer_mcpwm = true;
					mystatus.status_contrl_partd_suave = true;
					mystatus.status_contrl_discreto = false;
					mystatus.status_contrl_pot = true;
				}
				else if (botao_lido == botao_confirma && stts_bot_bx == true)
				{
					// partida suave ativado
					status_menu_aux2 = false;
					botao_lido = 0;
					vTaskDelay(500 / portTICK_PERIOD_MS);

					mystatus.status_timer_mcpwm = true;
					mystatus.status_contrl_partd_suave = true;
					mystatus.status_contrl_discreto = true;
					mystatus.status_contrl_pot = false;
				} // end else if

				vTaskDelay(400 / portTICK_PERIOD_MS);
			} // end while(status_menu_aux_2)
		} // end if(status_menu_aux2)

		vTaskDelay(400 / portTICK_PERIOD_MS);
	} // end while(1)
}


void ler_botoes(void *pvParameter)
{
	// inicializando os botões como entrada
	for (uint8_t i = 0; i < 8; i++)
	{
		esp_rom_gpio_pad_select_gpio(botoes[i]);
		gpio_set_direction(botoes[i], GPIO_MODE_INPUT);
		gpio_pulldown_dis(botoes[i]);
		gpio_pullup_dis(botoes[i]);
	}

	while (1)
	{
		for (uint8_t i = 0; i < 8; i++)
		{
			if (gpio_get_level(botoes[i]) == 1)
			{
				botao_lido = botoes[i];
			}
		}

		vTaskDelay(pdMS_TO_TICKS(50)); // delay de 50ms para deboucing dos botões
	}
}


void task1(void *pvParameter)
{
	static bool status_led = 0;
	while (1)
	{
		status_led = !status_led;
		gpio_set_level(GPIO_NUM_9, status_led);
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}


/*
==================================================================================================================================
												CONFIGURAÇÃO DO MCPWM
==================================================================================================================================
*/

void inicia_mcpwm(){
	ESP_LOGI(TAG, "criando MCPWM timer");
	mcpwm_timer_handle_t timer = NULL;
	mcpwm_timer_config_t timer_config = {
		.group_id = 0,
		.clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
		.resolution_hz = freq_resolution_mcpwm,
		.count_mode = MCPWM_TIMER_COUNT_MODE_UP_DOWN,
		.period_ticks = dados_pwm.amplitude_senoide,
	};
	ESP_ERROR_CHECK(mcpwm_new_timer(&timer_config, &timer));

	ESP_LOGI(TAG, "criando MCPWM operator");
	mcpwm_oper_handle_t operators[3];
	mcpwm_operator_config_t operator_config = {
		.group_id = 0,
	};
	for (int i = 0; i < 3; i++){
		ESP_ERROR_CHECK(mcpwm_new_operator(&operator_config, &operators[i]));
	}

	ESP_LOGI(TAG, "conectando operadores ao mesmo timer");
	for (int i = 0; i < 3; i++){
		ESP_ERROR_CHECK(mcpwm_operator_connect_timer(operators[i], timer));
	}

	ESP_LOGI(TAG, "criando comparadores");
	mcpwm_cmpr_handle_t comparators[3];
	mcpwm_comparator_config_t compare_config = {
		.flags.update_cmp_on_tez = true,
		.flags.update_cmp_on_tep = true,
	};
	for (int i = 0; i < 3; i++){
		ESP_ERROR_CHECK(mcpwm_new_comparator(operators[i], &compare_config, &comparators[i]));
		ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(comparators[i], 0)); // dados_pwm.amplitude_senoide / 2
	}

	ESP_LOGI(TAG, "criando MCPWM generators");
	mcpwm_gen_handle_t generators[3][2] = {};
	mcpwm_generator_config_t gen_config = {};
	const int gen_gpios[3][2] = {
		{fase_A_H, fase_A_L},
		{fase_B_H, fase_B_L},
		{fase_C_H, fase_C_L}
	};

	for (int i = 0; i < 3; i++){
		for (int j = 0; j < 2; j++){
			gen_config.gen_gpio_num = gen_gpios[i][j];
			ESP_ERROR_CHECK(mcpwm_new_generator(operators[i], &gen_config, &generators[i][j]));
		}
	}

	ESP_LOGI(TAG, "setando acoes dos geradores");	
	for (int i = 0; i < 3; i++){
		ESP_ERROR_CHECK(
			mcpwm_generator_set_actions_on_compare_event(generators[i][0],
				MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, comparators[i], MCPWM_GEN_ACTION_HIGH),
				MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_DOWN, comparators[i], MCPWM_GEN_ACTION_LOW),
				MCPWM_GEN_COMPARE_EVENT_ACTION_END()
			)
		);
	}

	ESP_LOGI(TAG, "configurando dead time");
	mcpwm_dead_time_config_t config_tempo_morto = {
		.posedge_delay_ticks = 80,
		.negedge_delay_ticks = 0,
		.flags.invert_output = true
	};
	for (int i = 0; i < 3; i++){
		ESP_ERROR_CHECK(mcpwm_generator_set_dead_time(generators[i][0], generators[i][0], &config_tempo_morto));
	}

	// saídas do PWM A e B serão complementares entre si
	config_tempo_morto = (mcpwm_dead_time_config_t){
		.posedge_delay_ticks = 0,
		.negedge_delay_ticks = 80,
		.flags.invert_output = false,
	};
	for (int i = 0; i < 3; i++){
		ESP_ERROR_CHECK(mcpwm_generator_set_dead_time(generators[i][0], generators[i][1], &config_tempo_morto));
	}

	ESP_LOGI(TAG, "habilitando interrupção por timer para ajustar o duty cycle");
	esp_timer_handle_t periodic_timer = NULL;
	const esp_timer_create_args_t args_periodic_timers = {
		.callback = mcpwm_isr_timer,
		.arg = comparators,
	};
	ESP_ERROR_CHECK(esp_timer_create(&args_periodic_timers, &periodic_timer));
	ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 50));

	ESP_LOGI(TAG, "iniciando o MCPWM_TIMER");
	ESP_ERROR_CHECK(mcpwm_timer_enable(timer));
	ESP_ERROR_CHECK(mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP));

	printf("MCPWM configurado\n");
}

// TEMPO MORTO ATIVO BAIXO COMPLEMENTAR

/*
	ESP_LOGI(TAG, "configurando dead time");
	mcpwm_dead_time_config_t config_tempo_morto = {
		.posedge_delay_ticks = 80,
		.negedge_delay_ticks = 0,
		.flags.invert_output = true
	};
	for (int i = 0; i < 3; i++)
	{
		ESP_ERROR_CHECK(mcpwm_generator_set_dead_time(generators[i][0], generators[i][0], &config_tempo_morto));
	}

	// saídas do PWM A e B serão complementares entre si
	config_tempo_morto = (mcpwm_dead_time_config_t){
		.posedge_delay_ticks = 0,
		.negedge_delay_ticks = 80,
		.flags.invert_output = false,
	};
	for (int i = 0; i < 3; i++)
	{
		ESP_ERROR_CHECK(mcpwm_generator_set_dead_time(generators[i][0], generators[i][1], &config_tempo_morto));
	}
*/

// TEMPO MORTO ATIVO ALTO COMPLEMENTAR

/*
	ESP_LOGI(TAG, "configurando dead time");
	mcpwm_dead_time_config_t config_tempo_morto = {
		.posedge_delay_ticks = 80,
		.negedge_delay_ticks = 0,
	};
	for (int i = 0; i < 3; i++)
	{
		ESP_ERROR_CHECK(mcpwm_generator_set_dead_time(generators[i][0], generators[i][0], &config_tempo_morto));
	}

	// saídas do PWM A e B serão complementares entre si
	config_tempo_morto = (mcpwm_dead_time_config_t){
		.posedge_delay_ticks = 0,
		.negedge_delay_ticks = 80,
		.flags.invert_output = true,
	};
	for (int i = 0; i < 3; i++)
	{
		ESP_ERROR_CHECK(mcpwm_generator_set_dead_time(generators[i][0], generators[i][1], &config_tempo_morto));
	}
*/
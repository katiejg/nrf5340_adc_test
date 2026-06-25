/**
 * @brief Analog reads using the nRFx DPPI
 * 
 */

#include <nrfx_example.h>
#include <saadc_examples_common.h>
#include <nrfx_saadc.h>

#define NRFX_LOG_MODULE                 EXAMPLE
#define NRFX_EXAMPLE_CONFIG_LOG_ENABLED 1
#define NRFX_EXAMPLE_CONFIG_LOG_LEVEL   3
#include <nrfx_log.h>

// Channel Configuartion
static const nrfx_saadc_channel_t multi_channels[] =
{
      NRFX_SAADC_DEFAULT_CHANNEL_SE(SAADC_CH0_AIN, 0),
      NRFX_SAADC_DEFAULT_CHANNEL_SE(SAADC_CH1_AIN, 1),
#if SAADC_MAX_CHANNELS == 3
      NRFX_SAADC_DEFAULT_CHANNEL_SE(SAADC_CH2_AIN, 2),
#endif
};

#define NUM_ADC_CHANNELS NRFX_ARRAY_SIZE(multi_channels)

/** @brief Array specifying GPIO pins used to test the functionality of SAADC. */
static uint8_t m_out_pins[NUM_ADC_CHANNELS] = {
    SAADC_CH0_LOOPBACK_PIN,
    SAADC_CH1_LOOPBACK_PIN,
#if SAADC_MAX_CHANNELS == 3
    SAADC_CH2_LOOPBACK_PIN
#endif
};

/** @brief Symbol specifying the number of SAADC samplings to trigger. */
#define SAMPLING_ITERATIONS 2

/** @brief Symbol specifying the resolution of the SAADC. */
#define RESOLUTION NRF_SAADC_RESOLUTION_12BIT

static nrf_saadc_value_t samples_buffer[NUM_ADC_CHANNELS];

/** @brief Flag indicating that sampling on every specified channel is finished and buffer ( @ref m_samples_buffer ) is filled with samples. */
static bool saadc_ready;

/** @brief GPIOTE instance used in the example. */
static nrfx_gpiote_t gpiote_inst = NRFX_GPIOTE_INSTANCE(NRF_GPIOTE_INST_GET(SAADC_GPIOTE_INST_IDX));

#if !defined(__ZEPHYR__)
/* Define an IRQ handler named nrfx_gpiote_<SAADC_GPIOTE_INST_IDX>_irq_handler. */
NRFX_INSTANCE_IRQ_HANDLER_DEFINE(gpiote, SAADC_GPIOTE_INST_IDX, &gpiote_inst);
#endif

/** 
 * @brief Function for handling SAADC driver events.
 */
static void saadc_handler(nrfx_saadc_evt_t const * p_event) {
      uint16_t samples_number;
}

int main(void) {
      int status;
      (void)status;

#if defined(__ZEPHYR__)
      IRQ_CONNECT(NRFX_IRQ_NUMBER_GET(NRF_SAADC), IRQ_PRIO_LOWEST, nrfx_saadc_irq_handler, 0, 0);
      IRQ_CONNECT(NRFX_IRQ_NUMBER_GET(NRF_GPIOTE_INST_GET(SAADC_GPIOTE_INST_IDX)), IRQ_PRIO_LOWEST,
                  nrfx_gpiote_irq_handler, &gpiote_inst, 0);
#endif
      NRFX_EXAMPLE_LOG_INIT();
      NRFX_LOG_INFO("Starting nrfx_saadc simple non-blocking example.");
      NRFX_EXAMPLE_LOG_PROCESS();

      status = nrfx_saadc_init(NRFX_SAADC_DEFAULT_CONFIG_IRQ_PRIORITY);
      NRFX_ASSERT(status == 0);

      status = nrfx_gpiote_init(&gpiote_inst, NRFX_GPIOTE_DEFAULT_CONFIG_IRQ_PRIORITY);
      NRFX_ASSERT(status == 0);
      NRFX_LOG_INFO("GPIOTE status: %s",
                  nrfx_gpiote_init_check(&gpiote_inst) ? "initialized" : "not initialized");
      
      for (uint8_t i = 0; i < NUM_ADC_CHANNELS; i++) {
            gpiote_pin_toggle_task_setup(&gpiote_inst, m_out_pins[i]);
      }

      uint32_t sampling_index = 0;
      uint32_t channels_mask = 0;

      // STATE_MULTIPLE_CONFIG
      NRFX_LOG_INFO("Multiple channels SAADC test.");
      status = nrfx_saadc_channels_config(multi_channels, NUM_ADC_CHANNELS);
      NRFX_ASSERT(status == 0);

      


}
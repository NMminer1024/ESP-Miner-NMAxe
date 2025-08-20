#ifndef __CH9434_H__
#define __CH9434_H__
#include <Arduino.h>
#include <SPI.h>

/* -----------------------------------------------------------------------------
 *                                   Macro definitions
 * -----------------------------------------------------------------------------
 */
/* Enable bit */
#define CH9434_ENABLE                   1
#define CH9434_DISABLE                  0

/* UART index */
#define CH9434_UART_IDX_0               0
#define CH9434_UART_IDX_1               1
#define CH9434_UART_IDX_2               2
#define CH9434_UART_IDX_3               3

/* Register operation */
#define CH9434_REG_OP_WRITE             0x80
#define CH9434_REG_OP_READ              0x00

/*  Registers */
#define CH9434_UART0_REG_OFFSET_ADD     0x00
#define CH9434_UART1_REG_OFFSET_ADD     0x10
#define CH9434_UART2_REG_OFFSET_ADD     0x20
#define CH9434_UART3_REG_OFFSET_ADD     0x30
	#define CH9434_UARTx_RBR_ADD          0
	#define CH9434_UARTx_THR_ADD          0
	#define CH9434_UARTx_IER_ADD          1
	#define CH9434_UARTx_IIR_ADD          2
	#define CH9434_UARTx_FCR_ADD          2
	#define CH9434_UARTx_LCR_ADD          3
		#define CH9434_UARTx_BIT_DLAB       (1<<7)
	#define CH9434_UARTx_MCR_ADD          4
	#define CH9434_UARTx_LSR_ADD          5
	#define CH9434_UARTx_MSR_ADD          6
	#define CH9434_UARTx_SCR_ADD          7
	#define CH9434_UARTx_DLL_ADD          0
	#define CH9434_UARTx_DLM_ADD          1

// TNOW related
#define CH9434_TNOW_CTRL_CFG_ADD        0x41
	#define CH9434_BIT_TNOW_OUT_POLAR     0xF0
	#define CH9434_BIT_TNOW_OUT_EN        0x0F
#define CH9434_FIFO_CTRL_ADD            0x42
	#define CH9434_FIFO_CTRL_TR           (1<<4)
	#define CH9434_FIFO_UART_IDX          0x0F
#define CH9434_FIFO_CTRL_L_ADD	        0x43
#define CH9434_FIFO_CTRL_H_ADD	        0x44

// Clock control
#define CH9434_CLK_CTRL_CFG_ADD         0x48
	#define CH9434_CLK_CTRL_MOD           (3<<6)
	#define CH9434_XT_POWER_EN            (1<<5)
	#define CH9434_CLK_DIV_MASK           0x1F
#define CH9434_SLEEP_MOD_CFG_ADD        0x4A

//GPIO related settings
#define CH9434_GPIO_FUNC_EN_0           0x50
#define CH9434_GPIO_FUNC_EN_1           0x51
#define CH9434_GPIO_FUNC_EN_2           0x52
#define CH9434_GPIO_FUNC_EN_3           0x53
//GPIO direction settings
#define CH9434_GPIO_DIR_MOD_0           0x54
#define CH9434_GPIO_DIR_MOD_1           0x55
#define CH9434_GPIO_DIR_MOD_2           0x56
#define CH9434_GPIO_DIR_MOD_3           0x57
//GPIO pull-up settings
#define CH9434_GPIO_PU_MOD_0            0x58
#define CH9434_GPIO_PU_MOD_1            0x59
#define CH9434_GPIO_PU_MOD_2            0x5A
#define CH9434_GPIO_PU_MOD_3            0x5B
//GPIO pull-down settings
#define CH9434_GPIO_PD_MOD_0            0x5C
#define CH9434_GPIO_PD_MOD_1            0x5D
#define CH9434_GPIO_PD_MOD_2            0x5E
#define CH9434_GPIO_PD_MOD_3            0x5F
//GPIO pin values
#define CH9434_GPIO_PIN_VAL_0           0x60
#define CH9434_GPIO_PIN_VAL_1           0x61
#define CH9434_GPIO_PIN_VAL_2           0x62
#define CH9434_GPIO_PIN_VAL_3           0x63

/*  UART parameters definitions */
/* FIFO Size */
#define CH9434_UART_FIFO_MODE_256       0         //256
#define CH9434_UART_FIFO_MODE_512       1         //512
#define CH9434_UART_FIFO_MODE_1024      2         //1024
#define CH9434_UART_FIFO_MODE_1280      3         //1280
/* Character Size */
#define CH9434_UART_5_BITS_PER_CHAR     5
#define CH9434_UART_6_BITS_PER_CHAR     6
#define CH9434_UART_7_BITS_PER_CHAR     7
#define CH9434_UART_8_BITS_PER_CHAR     8
/* Stop Bits */
#define CH9434_UART_ONE_STOP_BIT        1
#define CH9434_UART_TWO_STOP_BITS       2
/* Parity settings */
#define CH9434_UART_NO_PARITY           0x00                              // No parity
#define CH9434_UART_ODD_PARITY          0x01                              // Odd parity
#define CH9434_UART_EVEN_PARITY         0x02                              // Even parity
#define CH9434_UART_MARK_PARITY         0x03                              // Mark parity
#define CH9434_UART_SPACE_PARITY        0x04                              // Space parity

/* TNOW index */
#define CH9434_TNOW_POLAR_NORMAL        0                                 // Normal output
#define CH9434_TNOW_POLAR_OPPO          1                                 // Inverted output
               
#define CH9434_TNOW_0                   0
#define CH9434_TNOW_1                   1
#define CH9434_TNOW_2                   2
#define CH9434_TNOW_3                   3

/*  Low power mode */
#define CH9434_LOWPOWER_INVALID         0
#define CH9434_LOWPOWER_SLEEP           1

/* GPIO index */
#define CH9434_GPIO_0                   0
#define CH9434_GPIO_1                   1
#define CH9434_GPIO_2                   2
#define CH9434_GPIO_3                   3
#define CH9434_GPIO_4                   4
#define CH9434_GPIO_5                   5
#define CH9434_GPIO_6                   6
#define CH9434_GPIO_7                   7
#define CH9434_GPIO_8                   8
#define CH9434_GPIO_9                   9
#define CH9434_GPIO_10                  10
#define CH9434_GPIO_11                  11
#define CH9434_GPIO_12                  12
#define CH9434_GPIO_13                  13
#define CH9434_GPIO_14                  14
#define CH9434_GPIO_15                  15
#define CH9434_GPIO_16                  16
#define CH9434_GPIO_17                  17
#define CH9434_GPIO_18                  18
#define CH9434_GPIO_19                  19
#define CH9434_GPIO_20                  20
#define CH9434_GPIO_21                  21
#define CH9434_GPIO_22                  22
#define CH9434_GPIO_23                  23
#define CH9434_GPIO_24                  24

/* GPIO enable/disable */
#define CH9434_GPIO_ENABLE              1
#define CH9434_GPIO_DISABLE             0
 
/* GPIO direction settings */
#define CH9434_GPIO_DIR_IN              0
#define CH9434_GPIO_DIR_OUT             1
 
/* GPIO pull-up enable */
#define CH9434_GPIO_PU_ENABLE           1
#define CH9434_GPIO_PU_DISABLE          0

/* GPIO pull-down enable */
#define CH9434_GPIO_PD_ENABLE           1
#define CH9434_GPIO_PD_DISABLE          0

/* GPIO output settings */
#define CH9434_GPIO_SET                 1
#define CH9434_GPIO_RESET               0


class CH9434{
private:
    SPIClass        _spi;
    uint8_t         _rst_pin, _int_pin;
    uint8_t         _lower_power_reg;
    uint32_t        _osc_xt_frq, _sys_osc_frq;
    uint32_t        _ch9434_gpio_x_val;// 24 gpio pins
public:
    CH9434(SPIClass &port, uint8_t int_p, uint8_t rst_p, uint32_t osc_xt_freq = 32000000)
        :_spi(port), _rst_pin(rst_p), _int_pin(int_p), _osc_xt_frq(osc_xt_freq){
        pinMode(_rst_pin, OUTPUT);
        pinMode(_int_pin, INPUT_PULLUP);
        //reset chip
        digitalWrite(_rst_pin, LOW);
        delay(10);
        digitalWrite(_rst_pin, HIGH);
        delay(10);
    }
    ~CH9434(){
    }



    /**
     * Initialize the CH9434 chip.
     * @param xt_en Enable external crystal oscillator (non-zero to enable)
     * @param freq_mul_en Enable frequency multiplier (non-zero to enable)
     * @param div_num Clock divider value
     */
    void init(uint8_t xt_en, uint8_t freq_mul_en, uint8_t div_num){
        _spi.begin();
        _spi.setHwCs(false);
        _spi.setBitOrder(MSBFIRST);
        _spi.setDataMode(SPI_MODE0);
        _spi.setFrequency(1000*1000*10); // 10 MHz

    }
    /**
     * Record external crystal oscillator frequency.
     * @param x_freq Current external crystal frequency in Hz
     */
    void osc_xt_freq_set(uint32_t x_freq);

    /**
     * Initialize clock mode of CH9434.
     * @param xt_en Enable external crystal (non-zero to enable)
     * @param freq_mul_en Enable frequency multiplier (non-zero to enable)
     * @param div_num Clock divider value
     */
    void init_clk_mode(uint8_t xt_en, uint8_t freq_mul_en, uint8_t div_num);

    /**
     * Configure UART parameters.
     * @param uart_idx UART index
     * @param bps Baud rate
     * @param data_bits Number of data bits (5..8)
     * @param stop_bits Number of stop bits (1 or 2)
     * @param veri_bits Parity setting (use CH9434_UART_* parity macros)
     */
    void uartx_para_set(uint8_t uart_idx, uint32_t bps, uint8_t data_bits, uint8_t stop_bits, uint8_t veri_bits);

    /**
     * Configure UART FIFO.
     * @param uart_idx UART index
     * @param fifo_en Enable FIFO (non-zero to enable)
     * @param fifo_level FIFO trigger level
     */
    void uartx_fifo_set(uint8_t uart_idx, uint8_t fifo_en, uint8_t fifo_level);

    /**
     * Configure UART interrupts.
     * @param uart_idx UART index
     * @param modem Modem status interrupt enable
     * @param line Line status interrupt enable
     * @param tx Transmit interrupt enable
     * @param rx Receive interrupt enable
     */
    void uartx_irq_set(uint8_t uart_idx, uint8_t modem, uint8_t line, uint8_t tx, uint8_t rx);

    /**
     * Configure UART flow control.
     * @param uart_idx UART index
     * @param flow_en Enable flow control (non-zero to enable)
     */
    void uartx_flow_set(uint8_t uart_idx, uint8_t flow_en);

    /**
     * Enable UART interrupt requests.
     * @param uart_idx UART index
     */
    void uartx_irq_open(uint8_t uart_idx);

    /**
     * Set UART RTS/DTR pin values.
     * @param uart_idx UART index
     * @param rts_val RTS pin output value (0/1)
     * @param dtr_val DTR pin output value (0/1)
     */
    void uartx_rts_dtr_pin(uint8_t uart_idx, uint8_t rts_val, uint8_t dtr_val);

    /**
     * Write SRC register for a UART.
     * @param uart_idx UART index
     * @param src_val Value to write into SRC register
     */
    void uartx_write_src(uint8_t uart_idx, uint8_t src_val);

    /**
     * Read SRC register of a UART.
     * @param uart_idx UART index
     * @return SRC register value
     */
    uint8_t uartx_read_src(uint8_t uart_idx);

    /**
     * Read IIR (interrupt identification register) of a UART.
     * @param uart_idx UART index
     * @return IIR register value
     */
    uint8_t uartx_read_iir(uint8_t uart_idx);

    /**
     * Read LSR (line status register) of a UART.
     * @param uart_idx UART index
     * @return LSR register value
     */
    uint8_t uartx_read_lsr(uint8_t uart_idx);

    /**
     * Read MSR (modem status register) of a UART.
     * @param uart_idx UART index
     * @return MSR register value
     */
    uint8_t uartx_read_msr(uint8_t uart_idx);

    /**
     * Get current number of bytes in UART RX FIFO.
     * @param uart_idx UART index
     * @return Number of bytes available in RX FIFO
     */
    uint16_t uartx_get_rx_fifo_len(uint8_t uart_idx);

    /**
     * Read data from UART RX FIFO.
     * @param uart_idx UART index
     * @param p_data Buffer to store read bytes
     * @param read_len Number of bytes to read
     * @return Number of bytes actually read (or status code)
     */
    uint8_t uartx_get_rx_fifo_data(uint8_t uart_idx, uint8_t *p_data, uint16_t read_len);

    /**
     * Get current number of bytes in UART TX FIFO.
     * @param uart_idx UART index
     * @return Number of bytes currently in TX FIFO
     */
    uint16_t uartx_get_tx_fifo_len(uint8_t uart_idx);

    /**
     * Write data into UART TX FIFO.
     * @param uart_idx UART index
     * @param p_data Buffer containing data to send
     * @param send_len Number of bytes to send
     * @return Number of bytes written (or status code)
     */
    uint8_t uartx_set_tx_fifo_data(uint8_t uart_idx, uint8_t *p_data, uint16_t send_len);

    /**
     * Configure TNOW (RS-485 TX enable) behavior for UART.
     * @param uart_idx UART index
     * @param tnow_en Enable TNOW output (non-zero to enable)
     * @param polar Output polarity (use CH9434_TNOW_POLAR_* macros)
     */
    void uartx_tnow_set(uint8_t uart_idx, uint8_t tnow_en, uint8_t polar);

    /**
     * Set low power mode of CH9434.
     * @param mode Low power mode value (e.g. CH9434_LOWPOWER_SLEEP)
     */
    void lower_power_mode_set(uint8_t mode);

    /**
     * Wake up the CH9434 from low power mode.
     */
    void wake_up(void);

    /**
     * Configure GPIO function and settings on CH9434.
     * @param gpio_idx GPIO index
     * @param en Enable the GPIO function (non-zero to enable)
     * @param dir Direction (CH9434_GPIO_DIR_IN/OUT)
     * @param pu Pull-up enable (non-zero to enable)
     * @param pd Pull-down enable (non-zero to enable)
     */
    void gpio_func_set(uint8_t gpio_idx, uint8_t en, uint8_t dir, uint8_t pu, uint8_t pd);

    /**
     * Set GPIO output value on CH9434.
     * @param gpio_idx GPIO index
     * @param out_val Output value (0 or 1)
     */
    void gpio_pin_out(uint8_t gpio_idx, uint8_t out_val);

    /**
     * Read GPIO input value on CH9434.
     * @param gpio_idx GPIO index
     * @return GPIO level: 1 = high, 0 = low
     */
    uint8_t gpio_pin_val(uint8_t gpio_idx);
};


#endif
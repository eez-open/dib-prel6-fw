#define READ_PIN(PORT, PIN) ((PORT->IDR & PIN) ? 1 : 0)
#define SET_PIN(PORT, PIN) PORT->BSRR = PIN
#define RESET_PIN(PORT, PIN) PORT->BSRR = (uint32_t)PIN << 16
#define WRITE_PIN(PORT, PIN, STATE) if (STATE) SET_PIN(PORT, PIN); else RESET_PIN(PORT, PIN)

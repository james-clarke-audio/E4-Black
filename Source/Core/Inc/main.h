/* USER CODE BEGIN Header */

/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LED_ON_Pin GPIO_PIN_13
#define LED_ON_GPIO_Port GPIOC
#define IR_SIDE_RIGHT_Pin GPIO_PIN_14
#define IR_SIDE_RIGHT_GPIO_Port GPIOC
#define IR_FRONT_RIGHT_Pin GPIO_PIN_15
#define IR_FRONT_RIGHT_GPIO_Port GPIOC
#define LED_RIGHT_Pin GPIO_PIN_2
#define LED_RIGHT_GPIO_Port GPIOB
#define LED_LEFT_Pin GPIO_PIN_10
#define LED_LEFT_GPIO_Port GPIOB
#define BUTTON_LEFT_Pin GPIO_PIN_11
#define BUTTON_LEFT_GPIO_Port GPIOA
#define BUTTON_RIGHT_Pin GPIO_PIN_12
#define BUTTON_RIGHT_GPIO_Port GPIOA
#define IR_FRONT_LEFT_Pin GPIO_PIN_4
#define IR_FRONT_LEFT_GPIO_Port GPIOB
#define IR_SIDE_LEFT_Pin GPIO_PIN_5
#define IR_SIDE_LEFT_GPIO_Port GPIOB
/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */

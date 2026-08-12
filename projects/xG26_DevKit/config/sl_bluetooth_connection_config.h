/***************************************************************************//**
 * @file
 * @brief Bluetooth Connection configuration
 *******************************************************************************
 * # License
 * <b>Copyright 2023 Silicon Laboratories Inc. www.silabs.com</b>
 ******************************************************************************/

#ifndef SL_BT_CONNECTION_CONFIG_H
#define SL_BT_CONNECTION_CONFIG_H

// <<< Use Configuration Wizard in Context Menu >>>
// <o SL_BT_CONFIG_MAX_CONNECTIONS> Max number of connections reserved for user <0-32>
// <i> Default: 4
#define SL_BT_CONFIG_MAX_CONNECTIONS     (4)

// <o SL_BT_CONFIG_CONNECTION_DATA_LENGTH> Preferred maximum TX payload octets <27-251>
// <i> Default: 251
#define SL_BT_CONFIG_CONNECTION_DATA_LENGTH     (251)

// <<< end of configuration section >>>
#endif

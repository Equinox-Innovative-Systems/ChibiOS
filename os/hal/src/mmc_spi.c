/*
    ChibiOS/RT - Copyright (C) 2006,2007,2008,2009,2010,
                 2011,2012 Giovanni Di Sirio.

    This file is part of ChibiOS/RT.

    ChibiOS/RT is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    ChibiOS/RT is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
/*
   Parts of this file have been contributed by Matthias Blaicher.
 */

/**
 * @file    mmc_spi.c
 * @brief   MMC over SPI driver code.
 *
 * @addtogroup MMC_SPI
 * @{
 */

#include <string.h>

#include "ch.h"
#include "hal.h"

#if HAL_USE_MMC_SPI || defined(__DOXYGEN__)

/*===========================================================================*/
/* Driver local definitions.                                                 */
/*===========================================================================*/

/*===========================================================================*/
/* Driver exported variables.                                                */
/*===========================================================================*/

/*===========================================================================*/
/* Driver local variables.                                                   */
/*===========================================================================*/

/* Forward declarations required by mmc_vmt.*/
bool_t mmc_read(void *instance, uint32_t startblk,
                uint8_t *buffer, uint32_t n);
bool_t mmc_write(void *instance, uint32_t startblk,
                 const uint8_t *buffer, uint32_t n);

/**
 * @brief   Virtual methods table.
 */
static const struct MMCSDBlockDeviceVMT mmc_vmt = {
  (bool_t (*)(void *))mmc_lld_is_card_inserted,
  (bool_t (*)(void *))mmc_lld_is_write_protected,
  (bool_t (*)(void *))mmcConnect,
  (bool_t (*)(void *))mmcDisconnect,
  mmc_read,
  mmc_write,
  (bool_t (*)(void *))mmcSync,
  (bool_t (*)(void *, BlockDeviceInfo *))mmcGetInfo
};

/**
 * @brief   Lookup table for CRC-7 ( based on polynomial x^7 + x^3 + 1).
 */
static const uint8_t crc7_lookup_table[256] = {
  0x00, 0x09, 0x12, 0x1b, 0x24, 0x2d, 0x36, 0x3f, 0x48, 0x41, 0x5a, 0x53,
  0x6c, 0x65, 0x7e, 0x77, 0x19, 0x10, 0x0b, 0x02, 0x3d, 0x34, 0x2f, 0x26,
  0x51, 0x58, 0x43, 0x4a, 0x75, 0x7c, 0x67, 0x6e, 0x32, 0x3b, 0x20, 0x29,
  0x16, 0x1f, 0x04, 0x0d, 0x7a, 0x73, 0x68, 0x61, 0x5e, 0x57, 0x4c, 0x45,
  0x2b, 0x22, 0x39, 0x30, 0x0f, 0x06, 0x1d, 0x14, 0x63, 0x6a, 0x71, 0x78,
  0x47, 0x4e, 0x55, 0x5c, 0x64, 0x6d, 0x76, 0x7f, 0x40, 0x49, 0x52, 0x5b,
  0x2c, 0x25, 0x3e, 0x37, 0x08, 0x01, 0x1a, 0x13, 0x7d, 0x74, 0x6f, 0x66,
  0x59, 0x50, 0x4b, 0x42, 0x35, 0x3c, 0x27, 0x2e, 0x11, 0x18, 0x03, 0x0a,
  0x56, 0x5f, 0x44, 0x4d, 0x72, 0x7b, 0x60, 0x69, 0x1e, 0x17, 0x0c, 0x05,
  0x3a, 0x33, 0x28, 0x21, 0x4f, 0x46, 0x5d, 0x54, 0x6b, 0x62, 0x79, 0x70,
  0x07, 0x0e, 0x15, 0x1c, 0x23, 0x2a, 0x31, 0x38, 0x41, 0x48, 0x53, 0x5a,
  0x65, 0x6c, 0x77, 0x7e, 0x09, 0x00, 0x1b, 0x12, 0x2d, 0x24, 0x3f, 0x36,
  0x58, 0x51, 0x4a, 0x43, 0x7c, 0x75, 0x6e, 0x67, 0x10, 0x19, 0x02, 0x0b,
  0x34, 0x3d, 0x26, 0x2f, 0x73, 0x7a, 0x61, 0x68, 0x57, 0x5e, 0x45, 0x4c,
  0x3b, 0x32, 0x29, 0x20, 0x1f, 0x16, 0x0d, 0x04, 0x6a, 0x63, 0x78, 0x71,
  0x4e, 0x47, 0x5c, 0x55, 0x22, 0x2b, 0x30, 0x39, 0x06, 0x0f, 0x14, 0x1d,
  0x25, 0x2c, 0x37, 0x3e, 0x01, 0x08, 0x13, 0x1a, 0x6d, 0x64, 0x7f, 0x76,
  0x49, 0x40, 0x5b, 0x52, 0x3c, 0x35, 0x2e, 0x27, 0x18, 0x11, 0x0a, 0x03,
  0x74, 0x7d, 0x66, 0x6f, 0x50, 0x59, 0x42, 0x4b, 0x17, 0x1e, 0x05, 0x0c,
  0x33, 0x3a, 0x21, 0x28, 0x5f, 0x56, 0x4d, 0x44, 0x7b, 0x72, 0x69, 0x60,
  0x0e, 0x07, 0x1c, 0x15, 0x2a, 0x23, 0x38, 0x31, 0x46, 0x4f, 0x54, 0x5d,
  0x62, 0x6b, 0x70, 0x79
};

/*===========================================================================*/
/* Driver local functions.                                                   */
/*===========================================================================*/

bool_t mmc_read(void *instance, uint32_t startblk,
                uint8_t *buffer, uint32_t n) {

  if (mmcStartSequentialRead((MMCDriver *)instance, startblk))
    return TRUE;
  while (n > 0) {
    if (mmcSequentialRead((MMCDriver *)instance, buffer))
      return TRUE;
    buffer += MMCSD_BLOCK_SIZE;
    n--;
  }
  if (mmcStopSequentialRead((MMCDriver *)instance))
      return TRUE;
  return FALSE;
}

bool_t mmc_write(void *instance, uint32_t startblk,
                 const uint8_t *buffer, uint32_t n) {

  if (mmcStartSequentialWrite((MMCDriver *)instance, startblk))
      return TRUE;
  while (n > 0) {
      if (mmcSequentialWrite((MMCDriver *)instance, buffer))
          return TRUE;
      buffer += MMCSD_BLOCK_SIZE;
      n--;
  }
  if (mmcStopSequentialWrite((MMCDriver *)instance))
      return TRUE;
  return FALSE;
}

/**
 * @brief Calculate the MMC standard CRC-7 based on a lookup table.
 *
 * @param[in] crc       start value for CRC
 * @param[in] buffer    pointer to data buffer
 * @param[in] len       length of data
 * @return              Calculated CRC
 */
static uint8_t crc7(uint8_t crc, const uint8_t *buffer, size_t len) {

  while (len--)
    crc = crc7_lookup_table[(crc << 1) ^ (*buffer++)];
  return crc;
}

/**
 * @brief   Insertion monitor timer callback function.
 *
 * @param[in] p         pointer to the @p MMCDriver object
 *
 * @notapi
 */
static void tmrfunc(void *p) {
  MMCDriver *mmcp = p;

  chSysLockFromIsr();
  if (mmcp->cnt > 0) {
    if (mmc_lld_is_card_inserted(mmcp)) {
      if (--mmcp->cnt == 0) {
        mmcp->state = MMC_INSERTED;
        chEvtBroadcastI(&mmcp->inserted_event);
      }
    }
    else
      mmcp->cnt = MMC_POLLING_INTERVAL;
  }
  else {
    if (!mmc_lld_is_card_inserted(mmcp)) {
      mmcp->state = MMC_WAIT;
      mmcp->cnt = MMC_POLLING_INTERVAL;
      chEvtBroadcastI(&mmcp->removed_event);
    }
  }
  chVTSetI(&mmcp->vt, MS2ST(MMC_POLLING_DELAY), tmrfunc, mmcp);
  chSysUnlockFromIsr();
}

/**
 * @brief   Waits an idle condition.
 *
 * @param[in] mmcp      pointer to the @p MMCDriver object
 *
 * @notapi
 */
static void wait(MMCDriver *mmcp) {
  int i;
  uint8_t buf[4];

  for (i = 0; i < 16; i++) {
    spiReceive(mmcp->config->spip, 1, buf);
    if (buf[0] == 0xFF)
      return;
  }
  /* Looks like it is a long wait.*/
  while (TRUE) {
    spiReceive(mmcp->config->spip, 1, buf);
    if (buf[0] == 0xFF)
      break;
#ifdef MMC_NICE_WAITING
    /* Trying to be nice with the other threads.*/
    chThdSleep(1);
#endif
  }
}

/**
 * @brief   Sends a command header.
 *
 * @param[in] mmcp      pointer to the @p MMCDriver object
 * @param[in] cmd       the command id
 * @param[in] arg       the command argument
 *
 * @notapi
 */
static void send_hdr(MMCDriver *mmcp, uint8_t cmd, uint32_t arg) {
  uint8_t buf[6];

  /* Wait for the bus to become idle if a write operation was in progress.*/
  wait(mmcp);

  buf[0] = 0x40 | cmd;
  buf[1] = arg >> 24;
  buf[2] = arg >> 16;
  buf[3] = arg >> 8;
  buf[4] = arg;
  /* Calculate CRC for command header, shift to right position, add stop bit.*/
  buf[5] = ((crc7(0, buf, 5) & 0x7F) << 1) | 0x01;

  spiSend(mmcp->config->spip, 6, buf);
}

/**
 * @brief   Receives a single byte response.
 *
 * @param[in] mmcp      pointer to the @p MMCDriver object
 * @return              The response as an @p uint8_t value.
 * @retval 0xFF         timed out.
 *
 * @notapi
 */
static uint8_t recvr1(MMCDriver *mmcp) {
  int i;
  uint8_t r1[1];

  for (i = 0; i < 9; i++) {
    spiReceive(mmcp->config->spip, 1, r1);
    if (r1[0] != 0xFF)
      return r1[0];
  }
  return 0xFF;
}

/**
 * @brief   Receives a three byte response.
 *
 * @param[in] mmcp      pointer to the @p MMCDriver object
 * @param[out] buffer   pointer to four bytes wide buffer
 * @return              First response byte as an @p uint8_t value.
 * @retval 0xFF         timed out.
 *
 * @notapi
 */
static uint8_t recvr3(MMCDriver *mmcp, uint8_t* buffer) {
  uint8_t r1;

  r1 = recvr1(mmcp);
  spiReceive(mmcp->config->spip, 4, buffer);

  return r1;
}

/**
 * @brief   Sends a command an returns a single byte response.
 *
 * @param[in] mmcp      pointer to the @p MMCDriver object
 * @param[in] cmd       the command id
 * @param[in] arg       the command argument
 * @return              The response as an @p uint8_t value.
 * @retval 0xFF         timed out.
 *
 * @notapi
 */
static uint8_t send_command_R1(MMCDriver *mmcp, uint8_t cmd, uint32_t arg) {
  uint8_t r1;

  spiSelect(mmcp->config->spip);
  send_hdr(mmcp, cmd, arg);
  r1 = recvr1(mmcp);
  spiUnselect(mmcp->config->spip);
  return r1;
}

/**
 * @brief   Sends a command which returns a five bytes response (R3).
 *
 * @param[in] mmcp      pointer to the @p MMCDriver object
 * @param[in] cmd       the command id
 * @param[in] arg       the command argument
 * @param[out] response pointer to four bytes wide uint8_t buffer
 * @return              The first byte of the response (R1) as an @p
 *                      uint8_t value.
 * @retval 0xFF         timed out.
 *
 * @notapi
 */
static uint8_t send_command_R3(MMCDriver *mmcp, uint8_t cmd, uint32_t arg,
                               uint8_t *response) {
  uint8_t r1;

  spiSelect(mmcp->config->spip);
  send_hdr(mmcp, cmd, arg);
  r1 = recvr3(mmcp, response);
  spiUnselect(mmcp->config->spip);
  return r1;
}

/**
 * @brief   Reads the CSD.
 *
 * @param[in] mmcp      pointer to the @p MMCDriver object
 * @param[out] csd       pointer to the CSD buffer
 *
 * @return              The operation status.
 * @retval FALSE        the operation succeeded.
 * @retval TRUE         the operation failed.
 *
 * @notapi
 */
static bool_t read_CSD(MMCDriver *mmcp, uint32_t csd[4]) {
  unsigned i;
  uint8_t *bp, buf[16];

  spiSelect(mmcp->config->spip);
  send_hdr(mmcp, MMCSD_CMD_SEND_CSD, 0);
  if (recvr1(mmcp) != 0x00) {
    spiUnselect(mmcp->config->spip);
    return TRUE;
  }

  /* Wait for data availability.*/
  for (i = 0; i < MMC_WAIT_DATA; i++) {
    spiReceive(mmcp->config->spip, 1, buf);
    if (buf[0] == 0xFE) {
      uint32_t *wp;

      spiReceive(mmcp->config->spip, 16, buf);
      bp = buf;
      for (wp = &csd[3]; wp >= csd; wp--) {
        *wp = ((uint32_t)bp[0] << 24) | ((uint32_t)bp[1] << 16) |
              ((uint32_t)bp[2] << 8)  | (uint32_t)bp[3];
        bp += 4;
      }

      /* CRC ignored then end of transaction. */
      spiIgnore(mmcp->config->spip, 2);
      spiUnselect(mmcp->config->spip);

      return FALSE;
    }
  }
  return TRUE;
}

/**
 * @brief   Waits that the card reaches an idle state.
 *
 * @param[in] mmcp      pointer to the @p MMCDriver object
 *
 * @notapi
 */
static void sync(MMCDriver *mmcp) {
  uint8_t buf[1];

  spiSelect(mmcp->config->spip);
  while (TRUE) {
    spiReceive(mmcp->config->spip, 1, buf);
    if (buf[0] == 0xFF)
      break;
#ifdef MMC_NICE_WAITING
    chThdSleep(1);      /* Trying to be nice with the other threads.*/
#endif
  }
  spiUnselect(mmcp->config->spip);
}

/*===========================================================================*/
/* Driver exported functions.                                                */
/*===========================================================================*/

/**
 * @brief   MMC over SPI driver initialization.
 * @note    This function is implicitly invoked by @p halInit(), there is
 *          no need to explicitly initialize the driver.
 *
 * @init
 */
void mmcInit(void) {

}

/**
 * @brief   Initializes an instance.
 *
 * @param[out] mmcp         pointer to the @p MMCDriver object
 * @param[in] spip          pointer to the SPI driver to be used as interface
 * @param[in] lscfg         low speed configuration for the SPI driver
 * @param[in] hscfg         high speed configuration for the SPI driver
 * @param[in] is_protected  function that returns the card write protection
 *                          setting
 * @param[in] is_inserted   function that returns the card insertion sensor
 *                          status
 *
 * @init
 */
void mmcObjectInit(MMCDriver *mmcp) {

  mmcp->vmt = &mmc_vmt;
  mmcp->state = MMC_STOP;
  mmcp->config = NULL;
  mmcp->block_addresses = FALSE;
  chEvtInit(&mmcp->inserted_event);
  chEvtInit(&mmcp->removed_event);
}

/**
 * @brief   Configures and activates the MMC peripheral.
 *
 * @param[in] mmcp      pointer to the @p MMCDriver object
 * @param[in] config    pointer to the @p MMCConfig object. Must be @p NULL.
 *
 * @api
 */
void mmcStart(MMCDriver *mmcp, const MMCConfig *config) {

  chDbgCheck((mmcp != NULL) && (config == NULL), "mmcStart");

  chSysLock();
  chDbgAssert(mmcp->state == MMC_STOP, "mmcStart(), #1", "invalid state");
  mmcp->config = config;
  mmcp->state = MMC_WAIT;
  mmcp->cnt = MMC_POLLING_INTERVAL;
  chVTSetI(&mmcp->vt, MS2ST(MMC_POLLING_DELAY), tmrfunc, mmcp);
  chSysUnlock();
}

/**
 * @brief   Disables the MMC peripheral.
 *
 * @param[in] mmcp      pointer to the @p MMCDriver object
 *
 * @api
 */
void mmcStop(MMCDriver *mmcp) {

  chDbgCheck(mmcp != NULL, "mmcStop");

  chSysLock();
  chDbgAssert((mmcp->state != MMC_UNINIT) &&
              (mmcp->state != MMC_READING) &&
              (mmcp->state != MMC_WRITING),
              "mmcStop(), #1", "invalid state");
  if (mmcp->state != MMC_STOP) {
    mmcp->state = MMC_STOP;
    chVTResetI(&mmcp->vt);
  }
  chSysUnlock();
  spiStop(mmcp->config->spip);
}

/**
 * @brief   Performs the initialization procedure on the inserted card.
 * @details This function should be invoked when a card is inserted and
 *          brings the driver in the @p MMC_READY state where it is possible
 *          to perform read and write operations.
 * @note    It is possible to invoke this function from the insertion event
 *          handler.
 *
 * @param[in] mmcp      pointer to the @p MMCDriver object
 *
 * @return              The operation status.
 * @retval FALSE        the operation succeeded and the driver is now
 *                      in the @p MMC_READY state.
 * @retval TRUE         the operation failed.
 *
 * @api
 */
bool_t mmcConnect(MMCDriver *mmcp) {
  unsigned i;
  bool_t result;
  uint32_t csd[4];

  chDbgCheck(mmcp != NULL, "mmcConnect");

  chDbgAssert((mmcp->state != MMC_UNINIT) && (mmcp->state != MMC_STOP),
              "mmcConnect(), #1", "invalid state");

  if (mmcp->state == MMC_INSERTED) {
    /* Slow clock mode and 128 clock pulses.*/
    spiStart(mmcp->config->spip, mmcp->config->lscfg);
    spiIgnore(mmcp->config->spip, 16);

    /* SPI mode selection.*/
    i = 0;
    while (TRUE) {
      if (send_command_R1(mmcp, MMCSD_CMD_GO_IDLE_STATE, 0) == 0x01)
        break;
      if (++i >= MMC_CMD0_RETRY)
        return TRUE;
      chThdSleepMilliseconds(10);
    }

    /* Try to detect if this is a high capacity card and switch to block
       addresses if possible.
       This method is based on "How to support SDC Ver2 and high capacity cards"
       by ElmChan.*/
    uint8_t r3[4];
    if (send_command_R3(mmcp, MMCSD_CMD_SEND_IF_COND,
                        MMCSD_CMD8_PATTERN, r3) != 0x05) {

      /* Switch to SDHC mode.*/
      i = 0;
      while (TRUE) {
        if ((send_command_R1(mmcp, MMCSD_CMD_APP_CMD, 0) == 0x01) &&
            (send_command_R3(mmcp, MMCSD_CMD_APP_OP_COND,
                             0x400001aa, r3) == 0x00))
          break;

        if (++i >= MMC_ACMD41_RETRY)
          return TRUE;
        chThdSleepMilliseconds(10);
      }

      /* Execute dedicated read on OCR register */
      send_command_R3(mmcp, MMCSD_CMD_READ_OCR, 0, r3);

      /* Check if CCS is set in response. Card operates in block mode if set.*/
      if(r3[0] & 0x40)
        mmcp->block_addresses = TRUE;
    }

    /* Initialization.*/
    i = 0;
    while (TRUE) {
      uint8_t b = send_command_R1(mmcp, MMCSD_CMD_INIT, 0);
      if (b == 0x00)
        break;
      if (b != 0x01)
        return TRUE;
      if (++i >= MMC_CMD1_RETRY)
        return TRUE;
      chThdSleepMilliseconds(10);
    }

    /* Initialization complete, full speed.*/
    spiStart(mmcp->config->spip, mmcp->config->hscfg);

    /* Setting block size.*/
    if (send_command_R1(mmcp, MMCSD_CMD_SET_BLOCKLEN,
                        MMCSD_BLOCK_SIZE) != 0x00)
      return TRUE;

    /* Determine capacity.*/
    if (read_CSD(mmcp, csd))
      return TRUE;
    mmcp->capacity = mmcsdGetCapacity(csd);
    if (mmcp->capacity == 0)
      return TRUE;

    /* Transition to MMC_READY state (if not extracted).*/
    chSysLock();
    if (mmcp->state == MMC_INSERTED) {
      mmcp->state = MMC_READY;
      result = FALSE;
    }
    else
      result = TRUE;
    chSysUnlock();
    return result;
  }
  if (mmcp->state == MMC_READY)
    return FALSE;
  /* Any other state is invalid.*/
  return TRUE;
}

/**
 * @brief   Brings the driver in a state safe for card removal.
 *
 * @param[in] mmcp      pointer to the @p MMCDriver object
 * @return              The operation status.
 *
 * @retval FALSE        the operation succeeded and the driver is now
 *                      in the @p MMC_INSERTED state.
 * @retval TRUE         the operation failed.
 *
 * @api
 */
bool_t mmcDisconnect(MMCDriver *mmcp) {
  bool_t status;

  chDbgCheck(mmcp != NULL, "mmcDisconnect");

  chDbgAssert((mmcp->state != MMC_UNINIT) && (mmcp->state != MMC_STOP),
              "mmcDisconnect(), #1", "invalid state");
  switch (mmcp->state) {
  case MMC_READY:
    /* Wait for the pending write operations to complete.*/
    sync(mmcp);
    chSysLock();
    if (mmcp->state == MMC_READY)
      mmcp->state = MMC_INSERTED;
    chSysUnlock();
  case MMC_INSERTED:
    status = FALSE;
  default:
    status = TRUE;
  }
  spiStop(mmcp->config->spip);
  return status;
}

/**
 * @brief   Starts a sequential read.
 *
 * @param[in] mmcp      pointer to the @p MMCDriver object
 * @param[in] startblk  first block to read
 *
 * @return              The operation status.
 * @retval FALSE        the operation succeeded.
 * @retval TRUE         the operation failed.
 *
 * @api
 */
bool_t mmcStartSequentialRead(MMCDriver *mmcp, uint32_t startblk) {

  chDbgCheck(mmcp != NULL, "mmcStartSequentialRead");

  chSysLock();
  if (mmcp->state != MMC_READY) {
    chSysUnlock();
    return TRUE;
  }
  mmcp->state = MMC_READING;
  chSysUnlock();

  spiStart(mmcp->config->spip, mmcp->config->hscfg);
  spiSelect(mmcp->config->spip);

  if(mmcp->block_addresses)
    send_hdr(mmcp, MMCSD_CMD_READ_MULTIPLE_BLOCK, startblk);
  else
    send_hdr(mmcp, MMCSD_CMD_READ_MULTIPLE_BLOCK, startblk * MMCSD_BLOCK_SIZE);

  if (recvr1(mmcp) != 0x00) {
    spiUnselect(mmcp->config->spip);
    chSysLock();
    if (mmcp->state == MMC_READING)
      mmcp->state = MMC_READY;
    chSysUnlock();
    return TRUE;
  }
  return FALSE;
}

/**
 * @brief   Reads a block within a sequential read operation.
 *
 * @param[in] mmcp      pointer to the @p MMCDriver object
 * @param[out] buffer   pointer to the read buffer
 *
 * @return              The operation status.
 * @retval FALSE        the operation succeeded.
 * @retval TRUE         the operation failed.
 *
 * @api
 */
bool_t mmcSequentialRead(MMCDriver *mmcp, uint8_t *buffer) {
  int i;

  chDbgCheck((mmcp != NULL) && (buffer != NULL), "mmcSequentialRead");

  chSysLock();
  if (mmcp->state != MMC_READING) {
    chSysUnlock();
    return TRUE;
  }
  chSysUnlock();

  for (i = 0; i < MMC_WAIT_DATA; i++) {
    spiReceive(mmcp->config->spip, 1, buffer);
    if (buffer[0] == 0xFE) {
      spiReceive(mmcp->config->spip, MMCSD_BLOCK_SIZE, buffer);
      /* CRC ignored. */
      spiIgnore(mmcp->config->spip, 2);
      return FALSE;
    }
  }
  /* Timeout.*/
  spiUnselect(mmcp->config->spip);
  chSysLock();
  if (mmcp->state == MMC_READING)
    mmcp->state = MMC_READY;
  chSysUnlock();
  return TRUE;
}

/**
 * @brief   Stops a sequential read gracefully.
 *
 * @param[in] mmcp      pointer to the @p MMCDriver object
 *
 * @return              The operation status.
 * @retval FALSE        the operation succeeded.
 * @retval TRUE         the operation failed.
 *
 * @api
 */
bool_t mmcStopSequentialRead(MMCDriver *mmcp) {
  static const uint8_t stopcmd[] = {0x40 | MMCSD_CMD_STOP_TRANSMISSION,
                                    0, 0, 0, 0, 1, 0xFF};
  bool_t result;

  chDbgCheck(mmcp != NULL, "mmcStopSequentialRead");

  chSysLock();
  if (mmcp->state != MMC_READING) {
    chSysUnlock();
    return TRUE;
  }
  chSysUnlock();

  spiSend(mmcp->config->spip, sizeof(stopcmd), stopcmd);
/*  result = recvr1(mmcp) != 0x00;*/
  /* Note, ignored r1 response, it can be not zero, unknown issue.*/
  recvr1(mmcp);
  result = FALSE;
  spiUnselect(mmcp->config->spip);

  chSysLock();
  if (mmcp->state == MMC_READING)
    mmcp->state = MMC_READY;
  chSysUnlock();
  return result;
}

/**
 * @brief   Starts a sequential write.
 *
 * @param[in] mmcp      pointer to the @p MMCDriver object
 * @param[in] startblk  first block to write
 *
 * @return              The operation status.
 * @retval FALSE        the operation succeeded.
 * @retval TRUE         the operation failed.
 *
 * @api
 */
bool_t mmcStartSequentialWrite(MMCDriver *mmcp, uint32_t startblk) {

  chDbgCheck(mmcp != NULL, "mmcStartSequentialWrite");

  chSysLock();
  if (mmcp->state != MMC_READY) {
    chSysUnlock();
    return TRUE;
  }
  mmcp->state = MMC_WRITING;
  chSysUnlock();

  spiStart(mmcp->config->spip, mmcp->config->hscfg);
  spiSelect(mmcp->config->spip);
  if(mmcp->block_addresses)
    send_hdr(mmcp, MMCSD_CMD_WRITE_MULTIPLE_BLOCK, startblk);
  else
    send_hdr(mmcp, MMCSD_CMD_WRITE_MULTIPLE_BLOCK,
             startblk * MMCSD_BLOCK_SIZE);


  if (recvr1(mmcp) != 0x00) {
    spiUnselect(mmcp->config->spip);
    chSysLock();
    if (mmcp->state == MMC_WRITING)
      mmcp->state = MMC_READY;
    chSysUnlock();
    return TRUE;
  }
  return FALSE;
}

/**
 * @brief   Writes a block within a sequential write operation.
 *
 * @param[in] mmcp      pointer to the @p MMCDriver object
 * @param[out] buffer   pointer to the write buffer
 *
 * @return              The operation status.
 * @retval FALSE        the operation succeeded.
 * @retval TRUE         the operation failed.
 *
 * @api
 */
bool_t mmcSequentialWrite(MMCDriver *mmcp, const uint8_t *buffer) {
  static const uint8_t start[] = {0xFF, 0xFC};
  uint8_t b[1];

  chDbgCheck((mmcp != NULL) && (buffer != NULL), "mmcSequentialWrite");

  chSysLock();
  if (mmcp->state != MMC_WRITING) {
    chSysUnlock();
    return TRUE;
  }
  chSysUnlock();

  spiSend(mmcp->config->spip, sizeof(start), start);    /* Data prologue.   */
  spiSend(mmcp->config->spip, MMCSD_BLOCK_SIZE, buffer);/* Data.            */
  spiIgnore(mmcp->config->spip, 2);                     /* CRC ignored.     */
  spiReceive(mmcp->config->spip, 1, b);
  if ((b[0] & 0x1F) == 0x05) {
    wait(mmcp);
    return FALSE;
  }

  /* Error.*/
  spiUnselect(mmcp->config->spip);
  chSysLock();
  if (mmcp->state == MMC_WRITING)
    mmcp->state = MMC_READY;
  chSysUnlock();
  return TRUE;
}

/**
 * @brief   Stops a sequential write gracefully.
 *
 * @param[in] mmcp      pointer to the @p MMCDriver object
 *
 * @return              The operation status.
 * @retval FALSE        the operation succeeded.
 * @retval TRUE         the operation failed.
 *
 * @api
 */
bool_t mmcStopSequentialWrite(MMCDriver *mmcp) {
  static const uint8_t stop[] = {0xFD, 0xFF};

  chDbgCheck(mmcp != NULL, "mmcStopSequentialWrite");

  chSysLock();
  if (mmcp->state != MMC_WRITING) {
    chSysUnlock();
    return TRUE;
  }
  chSysUnlock();

  spiSend(mmcp->config->spip, sizeof(stop), stop);
  spiUnselect(mmcp->config->spip);

  chSysLock();
  if (mmcp->state == MMC_WRITING) {
    mmcp->state = MMC_READY;
    chSysUnlock();
    return FALSE;
  }
  chSysUnlock();
  return TRUE;
}

/**
 * @brief   Waits for card idle condition.
 *
 * @param[in] mmcp      pointer to the @p MMCDriver object
 *
 * @return              The operation status.
 * @retval FALSE        the operation succeeded.
 * @retval TRUE         the operation failed.
 *
 * @api
 */
bool_t mmcSync(MMCDriver *mmcp) {

  chDbgCheck(mmcp != NULL, "mmcSync");

  chSysLock();
  if (mmcp->state != MMC_READY) {
    chSysUnlock();
    return TRUE;
  }
  chSysUnlock();

  sync(mmcp);

  return FALSE;
}

/**
 * @brief   Returns the media info.
 *
 * @param[in] mmcp      pointer to the @p MMCDriver object
 * @param[out] bdip     pointer to a @p BlockDeviceInfo structure
 *
 * @return              The operation status.
 * @retval FALSE        the operation succeeded.
 * @retval TRUE         the operation failed.
 *
 * @api
 */
bool_t mmcGetInfo(MMCDriver *mmcp, BlockDeviceInfo *bdip) {


  chDbgCheck((mmcp != NULL) && (bdip != NULL), "mmcGetInfo");

  chSysLock();
  if (mmcp->state != MMC_READY) {
    chSysUnlock();
    return TRUE;
  }
  chSysUnlock();

  bdip->blk_num  = mmcp->capacity;
  bdip->blk_size = MMCSD_BLOCK_SIZE;

  return FALSE;
}

#endif /* HAL_USE_MMC_SPI */

/** @} */

/*
*   (C) Copyright 2011, Oli Kraus
*   (C) Copyright 2013, Andrew Kroll (xxxajk)
*   (C) Copyright 2016, Patrick Dowling
*
*   Low-level driver code for SH1106 OLED with spicy DMA transfer.
*   Author: Patrick Dowling (pld@gurkenkiste.com)
*
*   Command sequences adapted from https://github.com/olikraus/u8glib/blob/master/csrc/u8g_dev_ssd1306_128x64.c
*   SPI transfer command adapted from https://github.com/xxxajk/spi4teensy3
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of version 3 of the GNU General Public License as
*   published by the Free Software Foundation at http://www.gnu.org/licenses,
*   with Additional Permissions under term 7(b) that the original copyright
*   notice and author attibution must be preserved and under term 7(c) that
*   modified versions be marked as different from the original.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*/


#include <Arduino.h>
#include "SH1106_128x64_driver.h"
#include "../../OC_gpio.h"
#include "../../OC_options.h"
#include "../../util/util_SPIFIFO.h"

#define DMA_PAGE_TRANSFER
#ifdef DMA_PAGE_TRANSFER
#include <DMAChannel.h>
static DMAChannel page_dma;
static bool page_dma_active = false;
#endif
#ifndef SPI_SR_RXCTR
#define SPI_SR_RXCTR 0XF0
#endif

static uint8_t SH1106_data_start_seq[] = {
// u8g_dev_ssd1306_128x64_data_start
  0x10, /* set upper 4 bit of the col adr to 0 */
  0x00, /* set lower 4 bit of the col adr to 0 */
  0x00  /* 0xb0 | page */  
};

static uint8_t SH1106_init_seq[] = {
// u8g_dev_ssd1306_128x64_adafruit3_init_seq
	0xae,			/*Set Display Off */
	0xd5,0x80,		/*set Display Clock Divide Ratio/Oscillator Frequency */
	0xa8,0x3f,      /* multiplex ratio, duty = 1/32 */  
	0x40,		    /*Set Multiplex Ratio */
	0x20,0x02,    	/*Set Display Offset*/
	0xa1,			/*Set Segment Re-Map*/
	0xc8,			/*Set COM Output Scan Direction*/
	0xda,0x12,		/*Set COM Pins Hardware Configuration*/
	0x81,0x6f,		/*Set Current Control */
	0xd9,0xd3,		/*Set Pre-Charge Period */
	0xdb,0x20,		/*Set VCOMH Deselect Level */
	0x2e,           /* 2012-05-27: Deactivate scroll */  
	0xa4,			/*Set Entire Display On/Off */
	0xa6,			/*Set Normal/Inverse Display*/
};

static uint8_t SH1106_display_on_seq[] = {
  0xaf
};

/*static*/
void SH1106_128x64_Driver::Init() {
  pinMode(OLED_CS, OUTPUT);
  pinMode(OLED_RST, OUTPUT);
  pinMode(OLED_DC, OUTPUT);
  //SPI_init(); 

  // u8g_teensy::U8G_COM_MSG_INIT
  digitalWriteFast(OLED_RST, HIGH);
  delay(1);
  digitalWriteFast(OLED_RST, LOW);
  delay(10);
  digitalWriteFast(OLED_RST, HIGH);

  // u8g_dev_ssd1306_128x64_adafruit3_init_seq
  digitalWriteFast(OLED_CS, OLED_CS_INACTIVE); // U8G_ESC_CS(0),    
  ChangeSpeed(SPICLOCK_30MHz);
  digitalWriteFast(OLED_DC, LOW); // U8G_ESC_ADR(0),           /* instruction mode */

  digitalWriteFast(OLED_RST, LOW); // U8G_ESC_RST(1),           /* do reset low pulse with (1*16)+2 milliseconds */
  delay(20);
  digitalWriteFast(OLED_RST, HIGH);
  delay(20);
  ChangeSpeed(SPI_CLOCK_8MHz);
  digitalWriteFast(OLED_CS, OLED_CS_ACTIVE); // U8G_ESC_CS(1),             /* enable chip */

  SPI_send(SH1106_init_seq, sizeof(SH1106_init_seq));

  digitalWriteFast(OLED_CS, OLED_CS_INACTIVE); // U8G_ESC_CS(0), 
  ChangeSpeed(SPICLOCK_30MHz);

#ifdef DMA_PAGE_TRANSFER
  page_dma.destination((volatile uint8_t&)SPI0_PUSHR);
  page_dma.transferSize(1);
  page_dma.transferCount(kSubpageSize);
  page_dma.disableOnCompletion();
  page_dma.triggerAtHardwareEvent(DMAMUX_SOURCE_SPI0_TX);
  page_dma.disable();
#endif

  Clear();
}

/*static*/
void SH1106_128x64_Driver::Flush() {
#ifdef DMA_PAGE_TRANSFER
  // https://github.com/patrickdowling/O_C/commit/3dbaab16fa936a01d376a48c72aefbec4242f686
  // If app processing in frame N takes too long, the next frame starts
  // late; this leaves less time for frame N+1, and in N+2 the display CS line
  // would be pulled high too soon. Why this effect is more pronounced with
  // gcc >= 5.4.1 is a different mystery.
  if (page_dma_active) {
    while (!page_dma.complete()) { }
    while (0 != (SPI0_SR & 0x0000f000)); // SPIx_SR TXCTR
    while (!(SPI0_SR & SPI_SR_TCF));
    page_dma_active = false;

    digitalWriteFast(OLED_CS, OLED_CS_INACTIVE); // U8G_ESC_CS(0)
    page_dma.clearComplete();
    page_dma.disable();
    // DmaSpi.h::post_finishCurrentTransfer_impl
    SPI0_RSER = 0;
    SPI0_SR = 0xFF0F0000;
  }
#endif
}

static uint8_t empty_page[SH1106_128x64_Driver::kPageSize];

/*static*/
void SH1106_128x64_Driver::Clear() {
  memset(empty_page, 0, sizeof(kPageSize));

  SH1106_data_start_seq[2] = 0xb0 | 0;
  digitalWriteFast(OLED_DC, LOW);
  ChangeSpeed(SPI_CLOCK_8MHz);
  digitalWriteFast(OLED_CS, OLED_CS_ACTIVE);
  SPI_send(SH1106_data_start_seq, sizeof(SH1106_data_start_seq));
  digitalWriteFast(OLED_DC, HIGH);
  for (size_t p = 0; p < kNumPages; ++p)
    SPI_send(empty_page, kPageSize);
  digitalWriteFast(OLED_CS, OLED_CS_INACTIVE); // U8G_ESC_CS(0)

  digitalWriteFast(OLED_DC, LOW);
  digitalWriteFast(OLED_CS, OLED_CS_ACTIVE);
  SPI_send(SH1106_display_on_seq, sizeof(SH1106_display_on_seq));
  digitalWriteFast(OLED_DC, HIGH);
}

/*static*/
void SH1106_128x64_Driver::SendPage(uint_fast8_t index, uint_fast8_t subpage, const uint8_t *data) {
  int startCol = subpage * kSubpageSize;
  const uint8_t* startData = data + startCol;
  
  SH1106_data_start_seq[0] = 0x10 + (startCol >> 4);
  SH1106_data_start_seq[1] = 0x00 + (startCol & 0x0F);
  SH1106_data_start_seq[2] = 0xb0 | index;
  ChangeSpeed(SPI_CLOCK_8MHz);
  digitalWriteFast(OLED_DC, LOW); // U8G_ESC_ADR(0),           /* instruction mode */
  digitalWriteFast(OLED_CS, OLED_CS_ACTIVE); // U8G_ESC_CS(1),             /* enable chip */
  SPI_send(SH1106_data_start_seq, sizeof(SH1106_data_start_seq)); // u8g_WriteEscSeqP(u8g, dev, u8g_dev_ssd1306_128x64_data_start);
  digitalWriteFast(OLED_DC, HIGH); // /* data mode */

#ifdef DMA_PAGE_TRANSFER
  // DmaSpi.h::pre_cs_impl()
  SPI0_SR = 0xFF0F0000;
  SPI0_RSER = SPI_RSER_RFDF_RE | SPI_RSER_RFDF_DIRS | SPI_RSER_TFFF_RE | SPI_RSER_TFFF_DIRS;

  page_dma.sourceBuffer(startData, kSubpageSize);
  page_dma.enable(); // go
  page_dma_active = true;
#else
  SPI_send(data, kPageSize);
  digitalWriteFast(OLED_CS, OLED_CS_INACTIVE); // U8G_ESC_CS(0)
#endif
}

void SH1106_128x64_Driver::SPI_send(void *bufr, size_t n) {

  // adapted from https://github.com/xxxajk/spi4teensy3
  int i;
  int nf;
  uint8_t *buf = (uint8_t *)bufr;

  if (n & 1) {
    uint8_t b = *buf++;
    // clear any data in RX/TX FIFOs, and be certain we are in master mode.
    SPI0_MCR = SPI_MCR_MSTR | SPI_MCR_CLR_RXF | SPI_MCR_CLR_TXF | SPI_MCR_PCSIS(0x1F);
    SPI0_SR = SPI_SR_TCF;
    SPI0_PUSHR = SPI_PUSHR_CONT | b;
    while (!(SPI0_SR & SPI_SR_TCF));
    n--;
  }
  // clear any data in RX/TX FIFOs, and be certain we are in master mode.
  SPI0_MCR = SPI_MCR_MSTR | SPI_MCR_CLR_RXF | SPI_MCR_CLR_TXF | SPI_MCR_PCSIS(0x1F);
  // initial number of words to push into TX FIFO
  nf = n / 2 < 3 ? n / 2 : 3;
  // limit for pushing data into TX FIFO
  uint8_t* limit = buf + n;
  for (i = 0; i < nf; i++) {
    uint16_t w = (*buf++) << 8;
    w |= *buf++;
    SPI0_PUSHR = SPI_PUSHR_CONT | SPI_PUSHR_CTAS(1) | w;
  }
  // write data to TX FIFO
  while (buf < limit) {
          uint16_t w = *buf++ << 8;
          w |= *buf++;
          while (!(SPI0_SR & SPI_SR_RXCTR));
          SPI0_PUSHR = SPI_PUSHR_CONT | SPI_PUSHR_CTAS(1) | w;
          SPI0_POPR;
  }
  // wait for data to be sent
  while (nf) {
          while (!(SPI0_SR & SPI_SR_RXCTR));
          SPI0_POPR;
          nf--;
  }
}

/*static*/
void SH1106_128x64_Driver::AdjustOffset(uint8_t offset) {
}

/*static*/
void SH1106_128x64_Driver::ChangeSpeed(uint32_t speed) {
	uint32_t ctar = speed;
	ctar = speed;
	ctar |= (ctar & 0x0F) << 12;
	KINETISK_SPI0.CTAR0 = ctar | SPI_CTAR_FMSZ(7);
	KINETISK_SPI0.CTAR1 = ctar | SPI_CTAR_FMSZ(15);
}


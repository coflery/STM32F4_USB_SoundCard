/* Includes ------------------------------------------------------------------*/

#define	__IO	volatile

#include "usbd_audio_core.h"
#include "usbd_audio_out_if.h"
#include "stm32f4_discovery_audio_codec.h"
#include <stdio.h>
#include <string.h>

/*********************************************
   AUDIO Device library callbacks
 *********************************************/
static uint8_t  usbd_audio_Init       (void  *pdev, uint8_t cfgidx);
static uint8_t  usbd_audio_DeInit     (void  *pdev, uint8_t cfgidx);
static uint8_t  usbd_audio_Setup      (void  *pdev, USB_SETUP_REQ *req);
static uint8_t  usbd_audio_EP0_RxReady(void *pdev);
static uint8_t  usbd_audio_DataIn     (void *pdev, uint8_t epnum);
static uint8_t  usbd_audio_DataOut    (void *pdev, uint8_t epnum);
static uint8_t  usbd_audio_SOF        (void *pdev);
static uint8_t  usbd_audio_OUT_Incplt (void  *pdev);

/*********************************************
   AUDIO Requests management functions
 *********************************************/
static void AUDIO_Req_FeatureUnit(void *pdev, USB_SETUP_REQ *req);
static void AUDIO_Req_ClockSource(void *pdev, USB_SETUP_REQ *req);
static uint8_t  *USBD_audio_GetCfgDesc (uint8_t speed, uint16_t *length);
/**
  * @}
  */ 

/** @defgroup usbd_audio_Private_Variables
  * @{
  */ 
/* Main Buffer for Audio Data Out transfers and its relative pointers */
uint8_t  IsocOutBuff [TOTAL_OUT_BUF_SIZE * 2];
uint8_t* IsocOutWrPtr = IsocOutBuff;
uint8_t* IsocOutRdPtr = IsocOutBuff;

/* Main Buffer for Audio Control requests transfers and its relative variables */
uint8_t  AudioCtl[64];
uint8_t  AudioCtlCmd = 0;
uint32_t AudioCtlLen = 0;
uint8_t  AudioCtlCS  = 0;
uint8_t  AudioCtlUnit = 0;

static uint32_t PlayFlag = 0;

static __IO uint32_t  usbd_audio_AltSet = 0;
static uint8_t usbd_audio_CfgDesc[AUDIO_CONFIG_DESC_SIZE];

/* AUDIO interface class callbacks structure */
USBD_Class_cb_TypeDef  AUDIO_cb = 
{
  usbd_audio_Init,
  usbd_audio_DeInit,
  usbd_audio_Setup,
  NULL, /* EP0_TxSent */
  usbd_audio_EP0_RxReady,
  usbd_audio_DataIn,
  usbd_audio_DataOut,
  usbd_audio_SOF,
  NULL,
  usbd_audio_OUT_Incplt,   
  USBD_audio_GetCfgDesc,
#ifdef USB_OTG_HS_CORE  
  USBD_audio_GetCfgDesc, /* use same config as per FS */
#endif    
};

/* USB AUDIO device Configuration Descriptor */
static uint8_t usbd_audio_CfgDesc[AUDIO_CONFIG_DESC_SIZE] =
{
  /* Configuration 1 */
  0x09,                                 /* bLength */
  USB_CONFIGURATION_DESCRIPTOR_TYPE,    /* bDescriptorType */
  LOBYTE(AUDIO_CONFIG_DESC_SIZE),       /* wTotalLength */
  HIBYTE(AUDIO_CONFIG_DESC_SIZE),      
  0x02,                                 /* bNumInterfaces */
  0x01,                                 /* bConfigurationValue */
  0x00,                                 /* iConfiguration */
  0xC0,                                 /* bmAttributes BUS Powered*/
  0x32,                                 /* bMaxPower = 100 mA*/
  /* 09 byte*/
  
  0x08,
  0x0B,
  0x00,
  0x02,
  0x01,
  0x00,
  0x20,
  0x00,
  /* 08 byte */
  
  /* Standard AC Interface Descriptor */
  AUDIO_INTERFACE_DESC_SIZE,            /* bLength */
  USB_INTERFACE_DESCRIPTOR_TYPE,        /* bDescriptorType */
  0x00,                                 /* bInterfaceNumber */
  0x00,                                 /* bAlternateSetting */
  0x00,                                 /* bNumEndpoints */
  USB_DEVICE_CLASS_AUDIO,               /* bInterfaceClass */
  AUDIO_SUBCLASS_AUDIOCONTROL,          /* bInterfaceSubClass */
  AUDIO_IP_VERSION_02_00,               /* bInterfaceProtocol */
  0x00,                                 /* iInterface */
  /* 09 byte*/
  
  /* Class-specific AC Interface Descriptor */
  AUDIO_INTERFACE_DESC_SIZE,            /* bLength */
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,      /* bDescriptorType */
  AUDIO_CONTROL_HEADER,                 /* bDescriptorSubtype */
  0x00,                                 /* bcdADC */
  0x02,                                 /* 2.00 */
  AUDIO_HEADSET,                        /* bCategory */
  0x40,                                 /* wTotalLength */
  0x00,
  0x00,                                 /* bmControls */
  /* 09 byte*/
  
  /* USB HeadSet Clock Source Descriptor */
  AUDIO_20_CLK_SOURCE_DESC_SIZE,        /* bLength */
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,      /* bDescriptorType */
  AUDIO_CONTROL_CLOCK_SOURCE,           /* bDescriptorSubtype */
  AUDIO_CLK_ID,                         /* bClockID */
  0x01,                                 /* bmAttributes */
  0x07,                                 /* bmControls TODO */
  0x00,                                 /* bAssocTerminal */
  0x00,                                 /* iClockSource */
  /* 08 byte*/
  
  /* USB HeadSet Input Terminal Descriptor */
  AUDIO_20_IT_DESC_SIZE,                /* bLength */
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,      /* bDescriptorType */
  AUDIO_CONTROL_INPUT_TERMINAL,         /* bDescriptorSubtype */
  AUDIO_IT_ID,                          /* bTerminalID */
  0x01,                                 /* wTerminalType AUDIO_TERMINAL_USB_STREAMING   0x0201 */
  0x01,
  0x00,                                 /* bAssocTerminal */
  AUDIO_CLK_ID,                         /* bCSourceID*/
  0x02,                                 /* bNrChannels */
  0x03,                                 /* wChannelConfig 0x00000003  Stereo */
  0x00,
  0x00,
  0x00,
  0x00,                                 /* iChannelNames */
  0x00,                                 /* bmControls */
  0x00,
  0x00,                                 /* iTerminal */
  /* 17 byte*/
  
  /* USB HeadSet Audio Feature Unit Descriptor */
  0x12,                                 /* bLength */
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,      /* bDescriptorType */
  AUDIO_CONTROL_FEATURE_UNIT,           /* bDescriptorSubtype */
  AUDIO_FU_ID,                          /* bUnitID */
  AUDIO_IT_ID,                          /* bSourceID */
  AUDIO_20_CTL_MUTE(CONTROL_BITMAP_PROG)/* bmaControls(0) */
  | AUDIO_20_CTL_VOLUME(CONTROL_BITMAP_PROG),
  0x00,
  0x00,
  0x00,
  0x00,                                 /* bmaControls(1) */
  0x00,
  0x00,
  0x00,
  0x00,                                 /* bmaControls(2) */
  0x00,
  0x00,
  0x00,
  0x00,                                 /* iFeature */
  /* 18 byte*/
  
  /*USB HeadSet Output Terminal Descriptor */
  AUDIO_20_OT_DESC_SIZE,      			/* bLength */
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,      /* bDescriptorType */
  AUDIO_CONTROL_OUTPUT_TERMINAL,        /* bDescriptorSubtype */
  AUDIO_OT_ID,                          /* bTerminalID */
  0x02,                                 /* wTerminalType  0x0301: Speaker, 0x0302: Headphone*/
  0x03,
  0x00,                                 /* bAssocTerminal */
  AUDIO_FU_ID,                          /* bSourceID */
  AUDIO_CLK_ID,                         /* bCSourceID */
  0x00,                                 /* bmControls */
  0x00,
  0x00,                                 /* iTerminal */
  /* 12 byte*/
  
  /* USB HeadSet Standard AS Interface Descriptor - Audio Streaming Zero Bandwidth */
  /* Interface 1, Alternate Setting 0                                              */
  AUDIO_INTERFACE_DESC_SIZE,  			/* bLength */
  USB_INTERFACE_DESCRIPTOR_TYPE,        /* bDescriptorType */
  0x01,                                 /* bInterfaceNumber */
  0x00,                                 /* bAlternateSetting */
  0x00,                                 /* bNumEndpoints */
  USB_DEVICE_CLASS_AUDIO,               /* bInterfaceClass */
  AUDIO_SUBCLASS_AUDIOSTREAMING,        /* bInterfaceSubClass */
  AUDIO_IP_VERSION_02_00,               /* bInterfaceProtocol */
  0x00,                                 /* iInterface */
  /* 09 byte*/
  
  /* USB Speaker Standard AS Interface Descriptor - Audio Streaming Operational */
  /* Interface 1, Alternate Setting 1                                           */
  AUDIO_INTERFACE_DESC_SIZE,  			/* bLength */
  USB_INTERFACE_DESCRIPTOR_TYPE,        /* bDescriptorType */
  0x01,                                 /* bInterfaceNumber */
  0x01,                                 /* bAlternateSetting */
  0x01,                                 /* bNumEndpoints */
  USB_DEVICE_CLASS_AUDIO,               /* bInterfaceClass */
  AUDIO_SUBCLASS_AUDIOSTREAMING,        /* bInterfaceSubClass */
  AUDIO_IP_VERSION_02_00,               /* bInterfaceProtocol */
  0x00,                                 /* iInterface */
  /* 09 byte*/
  
  /* USB HeadSet Audio Streaming Interface Descriptor */
  AUDIO_20_STREAMING_INTERFACE_DESC_SIZE,  /* bLength */
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,      /* bDescriptorType */
  AUDIO_STREAMING_GENERAL,              /* bDescriptorSubtype */
  AUDIO_IT_ID,                          /* 0x01: bTerminalLink */
  0x00,                                 /* bmControls */
  AUDIO_FORMAT_TYPE_I,                  /* bFormatType */
  0x01,                                 /* bmFormats PCM */
  0x00,
  0x00,
  0x00,
  0x02,                                 /* bNrChannels */
  0x03,                                 /* bmChannelConfig */
  0x00,
  0x00,
  0x00,
  0x00,                                 /* iChannelNames */
  /* 16 byte*/
  
  /* USB Speaker Audio Type I Format Interface Descriptor */
  0x06,                                 /* bLength */
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,      /* bDescriptorType */
  AUDIO_STREAMING_FORMAT_TYPE,          /* bDescriptorSubtype */
  AUDIO_FORMAT_TYPE_I,                  /* bFormatType */
  HALF_WORD_BYTES,                      /* bSubslotSize */
  SAMPLE_BITS,                          /* bBitResolution */
  /* 6 byte*/
  
  /* Endpoint 1 - Standard Descriptor */
  AUDIO_20_STANDARD_ENDPOINT_DESC_SIZE, /* bLength */
  USB_ENDPOINT_DESCRIPTOR_TYPE,         /* bDescriptorType */
  AUDIO_OUT_EP,                         /* bEndpointAddress 3 out endpoint for Audio */
  USB_ENDPOINT_TYPE_ADAPTIVE,           /* bmAttributes */
  AUDIO_PACKET_SZE(USBD_AUDIO_FREQ),    /* XXXX wMaxPacketSize in Bytes (Freq(Samples)*2(Stereo)*2(HalfWord)) */
  0x01,                                 /* bInterval */
  /* 07 byte*/
  
  /* Endpoint - Audio Streaming Descriptor*/
  AUDIO_20_STREAMING_ENDPOINT_DESC_SIZE,/* bLength */
  AUDIO_ENDPOINT_DESCRIPTOR_TYPE,       /* bDescriptorType */
  AUDIO_ENDPOINT_GENERAL,               /* bDescriptor */
  0x00,                                 /* bmAttributes */
  0x00,                                 /* bmControls */
  0x00,                                 /* bLockDelayUnits */
  0x00,                                 /* wLockDelay */
  0x00
  /* 08 byte*/
} ;

/**
  * @}
  */ 

/** @defgroup usbd_audio_Private_Functions
  * @{
  */ 

static uint32_t sUSBD_AUDIO_FREQ = USBD_AUDIO_FREQ;
static uint32_t sAUDIO_OUT_PACKET = AUDIO_OUT_PACKET;

void usbd_ConfigureAudio(int mode)
{
    //UAC2.0
}

/**
* @brief  usbd_audio_Init
*         Initializes the AUDIO interface.
* @param  pdev: device instance
* @param  cfgidx: Configuration index
* @retval status
*/
static uint8_t  usbd_audio_Init (void  *pdev, 
                                 uint8_t cfgidx)
{  
  /* Open EP OUT */
  DCD_EP_Open(pdev,
              AUDIO_OUT_EP,
              sAUDIO_OUT_PACKET,
              USB_OTG_EP_ISOC);

  /* Initialize the Audio output Hardware layer */
  if (AUDIO_OUT_fops.Init(sUSBD_AUDIO_FREQ, DEFAULT_VOLUME, 0) != USBD_OK)
  {
    return USBD_FAIL;
  }
    
  /* Prepare Out endpoint to receive audio data */
  DCD_EP_PrepareRx(pdev,
                   AUDIO_OUT_EP,
                   (uint8_t*)IsocOutBuff,                        
                   sAUDIO_OUT_PACKET);
  
  return USBD_OK;
}

/**
* @brief  usbd_audio_Init
*         DeInitializes the AUDIO layer.
* @param  pdev: device instance
* @param  cfgidx: Configuration index
* @retval status
*/
static uint8_t  usbd_audio_DeInit (void  *pdev, 
                                   uint8_t cfgidx)
{ 
  DCD_EP_Close (pdev , AUDIO_OUT_EP);
  
  /* DeInitialize the Audio output Hardware layer */
  if (AUDIO_OUT_fops.DeInit(0) != USBD_OK)
  {
    return USBD_FAIL;
  }
  
  return USBD_OK;
}

/**
  * @brief  usbd_audio_Setup
  *         Handles the Audio control request parsing.
  * @param  pdev: instance
  * @param  req: usb requests
  * @retval status
  */
static uint8_t  usbd_audio_Setup (void  *pdev, 
                                  USB_SETUP_REQ *req)
{
  uint16_t len=USB_AUDIO_DESC_SIZ;
  uint8_t  *pbuf=usbd_audio_CfgDesc + 18;
  
  switch (req->bmRequest & USB_REQ_TYPE_MASK)
  {
    /* AUDIO Class Requests -------------------------------*/
  case USB_REQ_TYPE_CLASS :
    /*
     * bmRequest        bRequest    wValue              wIndex
     * 00100001B        CUR/RANGE    CS                 Entity ID
     * 10100001B                    and CN or MCD       and Interfaces
     *---------------------------------------------------------------------------
     * 00100010B        CUR/RANGE    CS                 Endpoint
     * 10100010B                     and CN or MCD
     *
     */
    switch (req->bmRequest & AUDIO_REQ_CLASS_TYPE_MASK) {
        case AUDIO_REQ_INTERFACE:
            switch (ENTITY_ID(req->wIndex)) {
                case AUDIO_FU_ID:
                    AUDIO_Req_FeatureUnit(pdev, req);
                    break;
                case AUDIO_CLK_ID:
                    AUDIO_Req_ClockSource(pdev, req);
                    break;
            }
            break;
        
        case AUDIO_REQ_ENDPOINT:
            break;
    }
    break;
    
    /* Standard Requests -------------------------------*/
  case USB_REQ_TYPE_STANDARD:
    switch (req->bRequest)
    {
    case USB_REQ_GET_DESCRIPTOR: 
      if( (req->wValue >> 8) == AUDIO_DESCRIPTOR_TYPE)
      {
#ifdef USB_OTG_HS_INTERNAL_DMA_ENABLED
        pbuf = usbd_audio_Desc;   
#else
        pbuf = usbd_audio_CfgDesc + 18;
#endif 
        len = MIN(USB_AUDIO_DESC_SIZ , req->wLength);
      }
      
      USBD_CtlSendData (pdev, 
                        pbuf,
                        len);
      break;
      
    case USB_REQ_GET_INTERFACE :
      USBD_CtlSendData (pdev,
                        (uint8_t *)&usbd_audio_AltSet,
                        1);
      break;
      
    case USB_REQ_SET_INTERFACE :
      if ((uint8_t)(req->wValue) < AUDIO_TOTAL_IF_NUM)
      {
        usbd_audio_AltSet = (uint8_t)(req->wValue);
        if ((uint8_t)(req->wIndex) == 1) {
            if ((uint8_t)(req->wValue) == 0) {
                STM_EVAL_LEDOff(LED6);
                STM_EVAL_LEDOn(LED3);
            } else if ((uint8_t)(req->wValue) == 1) {
                STM_EVAL_LEDOff(LED3);
                STM_EVAL_LEDOn(LED6);
            }
        }
      }
      else
      {
        /* Call the error management function (command will be naked) */
        USBD_CtlError (pdev, req);
      }
      break;
    }
  }
  return USBD_OK;
}

/**
  * @brief  usbd_audio_EP0_RxReady
  *         Handles audio control requests data.
  * @param  pdev: device instance
  * @retval status
  */
static uint8_t  usbd_audio_EP0_RxReady (void  *pdev)
{ 
  /* Check if an AudioControl request has been issued */
  if (AudioCtlCmd == AUDIO_REQ_SET_CUR)
  {/* In this driver, to simplify code, only SET_CUR request is managed */
    /* Check for which addressed unit the AudioControl request has been issued */
    if (AudioCtlUnit == AUDIO_FU_ID)
    {/* In this driver, to simplify code, only one unit is managed */
        if (AudioCtlCS == AUDIO_CONTROL_VOLUME) {
            AUDIO_OUT_fops.VolumeCtl(AudioCtl[0]);
        } else if (AudioCtlCS == AUDIO_CONTROL_MUTE) {
            AUDIO_OUT_fops.MuteCtl(AudioCtl[0]);
        }
      
      /* Reset the AudioCtlCmd variable to prevent re-entering this function */
      AudioCtlCmd = 0;
      AudioCtlLen = 0;
    }
  } 
  
  return USBD_OK;
}

/**
  * @brief  usbd_audio_DataIn
  *         Handles the audio IN data stage.
  * @param  pdev: instance
  * @param  epnum: endpoint number
  * @retval status
  */
static uint8_t  usbd_audio_DataIn (void *pdev, uint8_t epnum)
{
  return USBD_OK;
}

/**
  * @brief  usbd_audio_DataOut
  *         Handles the Audio Out data stage.
  * @param  pdev: instance
  * @param  epnum: endpoint number
  * @retval status
  */
static uint8_t  usbd_audio_DataOut (void *pdev, uint8_t epnum)
{     
  if (epnum == AUDIO_OUT_EP)
  {
    /* Increment the Buffer pointer or roll it back when all buffers are full */
    if (IsocOutWrPtr >= (IsocOutBuff + (sAUDIO_OUT_PACKET * OUT_PACKET_NUM)))
    {/* All buffers are full: roll back */
      IsocOutWrPtr = IsocOutBuff;
    }
    
    /* Toggle the frame index */  
    ((USB_OTG_CORE_HANDLE*)pdev)->dev.out_ep[epnum].even_odd_frame = 
      (((USB_OTG_CORE_HANDLE*)pdev)->dev.out_ep[epnum].even_odd_frame)? 0:1;
      
    /* Prepare Out endpoint to receive next audio packet */
    DCD_EP_PrepareRx(pdev,
                     AUDIO_OUT_EP,
                     (uint8_t*)(IsocOutWrPtr),
                     sAUDIO_OUT_PACKET);

    /* Increment the buffer pointer */
    IsocOutWrPtr += sAUDIO_OUT_PACKET;
    
    /* Trigger the start of streaming only when half buffer is full */
    if ((PlayFlag == 0) && (IsocOutWrPtr >= (IsocOutBuff + ((sAUDIO_OUT_PACKET * OUT_PACKET_NUM) / 2))))
    {
      /* Enable start of Streaming */
      PlayFlag = 1;
    }
  }
  
  return USBD_OK;
}

/**
  * @brief  usbd_audio_SOF
  *         Handles the SOF event (data buffer update and synchronization).
  * @param  pdev: instance
  * @param  epnum: endpoint number
  * @retval status
  */
static uint8_t  usbd_audio_SOF (void *pdev)
{     
  /* Check if there are available data in stream buffer.
    In this function, a single variable (PlayFlag) is used to avoid software delays.
    The play operation must be executed as soon as possible after the SOF detection. */
  if (PlayFlag)
  {    
    /* Increment the Buffer pointer or roll it back when all buffers all full */  
    if (IsocOutRdPtr >= (IsocOutBuff + (sAUDIO_OUT_PACKET * OUT_PACKET_NUM)))
    {/* Roll back to the start of buffer */
      IsocOutRdPtr = IsocOutBuff;
    }
    /* Start playing received packet */
    AUDIO_OUT_fops.AudioCmd((uint8_t*)(IsocOutRdPtr),  /* Samples buffer pointer */
                            sAUDIO_OUT_PACKET,          /* Number of samples in Bytes */
                            AUDIO_CMD_PLAY);           /* Command to be processed */
    /* Increment to the next sub-buffer */
    IsocOutRdPtr += sAUDIO_OUT_PACKET;
    
    /* If all available buffers have been consumed, stop playing */
    if (IsocOutRdPtr == IsocOutWrPtr)
    {
      /* Pause the audio stream */
      AUDIO_OUT_fops.AudioCmd((uint8_t*)(IsocOutBuff),   /* Samples buffer pointer */
                              sAUDIO_OUT_PACKET,          /* Number of samples in Bytes */
                              AUDIO_CMD_PAUSE);          /* Command to be processed */
      
      /* Stop entering play loop */
      PlayFlag = 0;
      
      /* Reset buffer pointers */
      IsocOutRdPtr = IsocOutBuff;
      IsocOutWrPtr = IsocOutBuff;
    }
  }
  
  return USBD_OK;
}

/**
  * @brief  usbd_audio_OUT_Incplt
  *         Handles the iso out incomplete event.
  * @param  pdev: instance
  * @retval status
  */
static uint8_t  usbd_audio_OUT_Incplt (void  *pdev)
{
  return USBD_OK;
}


/******************************************************************************
     AUDIO Class requests management
******************************************************************************/
static void AUDIO_Req_ClockSource(void *pdev, USB_SETUP_REQ *req)
{
    switch (CS_ID(req->wValue)) {
        case CS_CONTROL_UNDEFINED:
            USBD_CtlError (pdev, req);
            break;
        
        case CS_SAM_FREQ_CONTROL:
            switch (req->bRequest) {
                case AUDIO_REQ_CUR:

                    if (req->bmRequest & AUDIO_REQ_GET_MASK) {
                        //Get Layout 3 parameter block
                        uint8_t para_block[4] = {
                            0x80,
                            0xBB,
                            0x00,
                            0x00
                        };
                        USBD_CtlSendData(pdev, 
                            para_block,
                            sizeof(para_block));
                    } else {
                        //Set
                        uint8_t para_block[4] = {0};
                        USBD_CtlPrepareRx (pdev, 
                            para_block,
                            req->wLength);
                    }
                    break;
                
                case AUDIO_REQ_RANGE:
                    if (req->bmRequest & AUDIO_REQ_GET_MASK) {
                        //Get Layout 3 parameter block
                        uint8_t para_block[14] = {
                            0x01,           /* wNumSubRanges */
                            0x00,
                            0x80,           /* dMIN(1) */
                            0xBB,
                            0x00,
                            0x00,
                            0x00,           /* dMAX(1) */
                            0xEE,
                            0x02,
                            0x00,
                            0x80,           /* dRES(1) */
                            0xBB,
                            0x00,
                            0x00
                        };
                        USBD_CtlSendData(pdev, 
                            para_block,
                            sizeof(para_block));
                    } else {
                        //Set
                    }
                    break;
                   
                default:
                    USBD_CtlError (pdev, req);
            }
            break;
        
        case CS_CLOCK_VALID_CONTROL:
            if ((req->bmRequest & AUDIO_REQ_GET_MASK) &&
                (req->bRequest == AUDIO_REQ_CUR)) {
                // Only Get CUR Request, Layout 1 parameter block
                uint8_t para_block[1] = {1};
                USBD_CtlSendData(pdev, 
                            para_block,
                            sizeof(para_block));
            } else {
                USBD_CtlError (pdev, req);
            }
            break;
        
        default:
            USBD_CtlError (pdev, req);
            return;
    }
}

static void AUDIO_Req_FeatureUnit(void *pdev, USB_SETUP_REQ *req)
{
  uint8_t bCS = CS_ID(req->wValue);
  uint8_t bCN = CN_ID(req->wValue);
  
  memset(AudioCtl, 0, sizeof(AudioCtl));
  if (bCS == AUDIO_CONTROL_VOLUME && bCN == 0) {
    //Layout 2 Parameter
    switch (req->bRequest) {
        case AUDIO_REQ_CUR:
          if (req->bmRequest & AUDIO_REQ_GET_MASK) {
            //GET
            AudioCtl[0] = 50;
            USBD_CtlSendData (pdev, AudioCtl, req->wLength);
          } else {
            //SET
            /* Prepare the reception of the buffer over EP0 */
            USBD_CtlPrepareRx (pdev, 
                    AudioCtl,
                    req->wLength);
    
            /* Set the global variables indicating current request and its length 
            to the function usbd_audio_EP0_RxReady() which will process the request */
            AudioCtlCmd = AUDIO_REQ_SET_CUR;     /* Set the request value */
            AudioCtlLen = req->wLength;          /* Set the request data length */
            AudioCtlCS  = bCS;
            AudioCtlUnit = HIBYTE(req->wIndex);  /* Set the request target unit */            
          }
          break;

        case AUDIO_REQ_RANGE:
          if (req->bmRequest & AUDIO_REQ_GET_MASK) {
            //GET
            AudioCtl[0] = 1;    //wNumSubRanges
            AudioCtl[1] = 0;
            AudioCtl[2] = 0;    //wMIN(1)
            AudioCtl[3] = 0;
            AudioCtl[4] = 100;  //wMAX(1)
            AudioCtl[5] = 0;
            AudioCtl[6] = 1;    //wRES(1)
            AudioCtl[7] = 0;
            USBD_CtlSendData (pdev, AudioCtl, req->wLength);
          } else {
            //SET
            USBD_CtlError (pdev, req);
          }
          break;

        default:
          USBD_CtlError (pdev, req);
          return;
    }
  } else if (bCS == AUDIO_CONTROL_MUTE && bCN == 0) {
    //Layout 2 Parameter
    switch (req->bRequest) {
        case AUDIO_REQ_CUR:
            if (req->bmRequest & AUDIO_REQ_GET_MASK) {
                //GET
                AudioCtl[0] = 0; /* bCUR */
                USBD_CtlSendData (pdev, AudioCtl, req->wLength);
            } else {
                //SET
                /* Prepare the reception of the buffer over EP0 */
                USBD_CtlPrepareRx (pdev, 
                        AudioCtl,
                        req->wLength);
                /* Set the global variables indicating current request and its length 
                to the function usbd_audio_EP0_RxReady() which will process the request */
                AudioCtlCmd = AUDIO_REQ_SET_CUR;     /* Set the request value */
                AudioCtlLen = req->wLength;          /* Set the request data length */
                AudioCtlCS  = bCS;
                AudioCtlUnit = HIBYTE(req->wIndex);  /* Set the request target unit */
            }
            break;

        default:
          USBD_CtlError (pdev, req);
          return;
    }
  }
}

/**
  * @brief  USBD_audio_GetCfgDesc 
  *         Returns configuration descriptor.
  * @param  speed : current device speed
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t  *USBD_audio_GetCfgDesc (uint8_t speed, uint16_t *length)
{
  *length = sizeof (usbd_audio_CfgDesc);
  return usbd_audio_CfgDesc;
}

/**
  * @}
  */ 

/**
  * @}
  */ 

/**
  * @}
  */ 

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/

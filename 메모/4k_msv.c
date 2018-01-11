#include <stdio.h>
#include <stdlib.h>
#include "vkernel.h"
#include "shell.h"
#include "ddi.h"
#include "hal.h"
#include "mw_msg.h"
#include "dcm.h"
#include "4k_msv.h"
#include "video.h"
#include "network.h"
#include "front.h"
#include "event.h"
#include "crc.h"
#include "libedid.h"
#include "userconsole.h"

#define SAFE_FREE(a) if(a!=NULL) {free(a); a=NULL};

#define APP_NAME "APP:4KMSV"

#define APPL_DBG(FORMAT, ...) DDI_UART_Print("[%-10s]" FORMAT, APP_NAME, ##__VA_ARGS__);
#define APPL_PRINT(FORMAT, ...) DDI_UART_Print(FORMAT, ##__VA_ARGS__);

#define MAX_BANDWIDTH 9.5
#define SINGLE_WIN_MAXBW 8.0

#define SUPPORT_EDIT_BY_SCREEN

#define REMOVED 	(0)
#define BUSY		(1)
#define READY		(2)
#define SIGNALOFF	(3) 
#define SIGNALON	(4)

#define VIDEO_STOP	(0)
#define VIDEO_START	(1)

#define VIDEO_INPUT_HMDI (0)
#define VIDEO_INPUT_DISP (1)
#define VIDEO_INPUT_AUTO (2)

#define RELATIVE_SCREEN_WIDTH	3840.0f
#define RELATIVE_SCREEN_HEIGHT	2160.0f
#define round(x) ((x)>=0?(long)((x)+0.5):(long)((x)-0.5))

typedef enum
{
	FRONT_BTN_QUAD = 0,
	FRONT_BTN_3SIDE = 1,
	FRONT_BTN_3BOTTOM = 2,
	FRONT_BTN_1 = 5,
	FRONT_BTN_2 = 6,
	FRONT_BTN_3 = 7,
	FRONT_BTN_4 = 8,
	FRONT_BTN_USER = 9,
	FRONT_BTN_POWER = 0x10,
	FRONT_BTN_RECALL = 0x13,
	FRONT_BTN_OK    = 0x30,
	FRONT_BTN_INPUT = 0x35,	
} FRONT_BUTTON;

typedef struct 
{
	UI8 input_ch[4];
	UI8 priority[4];
	UI8 Hwtype[4];
	VIDEO_RECT rect[4];
} APPLY_RECV_DATA;

typedef struct 
{
	UI16 usermode;
	VIDEO_RECT rect[MAX_4K_MSV_INPUT];
	UI8 priority[4];
	UI8 inputch[4];
} SAVE_USER_RECV_DATA;


typedef struct
{
	UI16 width;
	UI16 height;
	UI16 fps;
	UI16 beforeW;
	UI16 beforeH;
}	OUT_PANNEL_INFO;


typedef struct 
{
	VIDEO_RECT rect;
	BOOL needDivide;
	UI8 real_winnum;
} INPUT_PANNEL_INFO;

typedef struct 
{
	VIDEO_RECT win[MAX_4K_MSV_INPUT];
} PRESET_INFO_LAYOUT;

typedef struct 
{
	UI16 InHwNum;
	UI16 InHwType;
	UI16 InSourceType;
	UI16 hActive;
	UI16 vActive;
	UI16 framerate;
} VIDEO_INFO_CHANNEL;

typedef struct
{
	VIDEO_RECT rect;
	BOOL needDivide;
} 	VIDEO_INFO_PANNEL;

typedef struct 
{
	SCREEN_LAYOUT_MODE layout;
	VIDEO_INFO_CHANNEL input[MAX_4K_MSV_INPUT];
	VIDEO_INFO_PANNEL panel[MAX_4K_MSV_INPUT];
	int real_winnum[MAX_4K_MSV_INPUT];
} VIDEO_INFO_ALL;

typedef struct 
{
	UI16 Layoutmode;
	UI16 major_version;
	UI16 minor_version;
	UI16 output_format;	
	PRESET_INFO_LAYOUT preset[SCREEN_LAYOUT_END];		
	VIDEO_INFO_CHANNEL input[MAX_4K_MSV_INPUT];

	UI16 priority[MAX_4K_MSV_INPUT];
	UI16 input_ch[MAX_4K_MSV_INPUT];
	UI16 user_prio[MAX_4K_MSV_USER][MAX_4K_MSV_INPUT];
	UI16 user_ch[MAX_4K_MSV_USER][MAX_4K_MSV_INPUT];

	VIDEO_RECT WinRect[MAX_4K_MSV_INPUT];		// cur screen data
	
}	GUI_INFO_TABLE;


static GUI_INFO_TABLE guiInfoTable;




typedef struct 
{
	VK_HANDLE hEvtMessage;
	VK_HANDLE hWorkThread;
} APP_RESOURCE;

typedef struct 
{
	int type;
	int deviceState;
	int signalState;
	int window_number;
	UI8 edid[256];
	BOOL isStarted;
	BOOL isDivided;
	BOOL needScale;
	VIDEO_DATA videoData;
	VIDEO_HANDLE videoHandle;
	VIDEO_SEL_INPUT inputSource;
	VIDEO_SEL_INPUT currSource;
} VIDEO_DEVICE;


typedef struct 
{
	VIDEO_RECT win[4];
} PRESET_LAYOUT_INFO;


typedef struct 
{
	UI8 layout;
	UI16 outFormat;
	PRESET_LAYOUT_INFO preset_rect[SCREEN_LAYOUT_END];

	UI8 user_prio[4][4];
	UI8 user_input_ch[4][4];

	UI8 priority[4];
	UI8 input_ch[4];
} VIDEO_CONTROL_INFO;

typedef struct 
{
	HUINT32 crc32;
	VIDEO_CONTROL_INFO info;
} SAVE_DATA;

typedef struct 
{
	APP_RESOURCE	resource;
	VIDEO_DEVICE	txdev[MAX_4K_MSV_INPUT];
	VIDEO_DEVICE	rxdev[MAX_4K_MSV_OUTPUT];
	VIDEO_SURFACE	*screen[SCREEN_LAYOUT_END];
	SAVE_DATA 		data;
	OUT_PANNEL_INFO	outpannel;
	INPUT_PANNEL_INFO inputpannel[MAX_4K_MSV_INPUT];
	BOOL			enableGUIControl;
	BOOL			userBtnOnflag;
	BOOL			inputBtnOnflag;
} APP_CONTROL_BLOCK;

char *layout_to_string[SCREEN_LAYOUT_END] =
{
	"quad",
	"side",
	"bttom",
	"ch1",
	"ch2",
	"ch3",
	"ch4",
	"user1",
	"user2",
	"user3",
	"user4",
};

char *inputSource_to_string[VIDEO_INPUT_END] =
{
	"HDMI",
	"DISP",
	"AUTO",
	"UNKOWN",
};



static APP_CONTROL_BLOCK appControlblock;



int priority[4] = {1,2,3,0};
int cnt = 0;


static int _event_handler_net(APP_MSG *pmsg);
static int _event_handler_video(APP_MSG *pmsg);
static int _event_handler_front(APP_MSG *pmsg);
static int app_work_thread(void *p);

static BOOL isSameLayout(SCREEN_LAYOUT_MODE mode);
static BOOL isMultiviewMode(SCREEN_LAYOUT_MODE mode);
static BOOL isNeedStop(SCREEN_LAYOUT_MODE mode0,SCREEN_LAYOUT_MODE mode1,VIDEO_STREAM *pStream);
static int  layout2device(SCREEN_LAYOUT_MODE mode);
static SCREEN_LAYOUT_MODE preset2layout(PRESET_MODE preset);

static PRESET_MODE layout2preset(SCREEN_LAYOUT_MODE layout);
static SCREEN_LAYOUT_MODE frontBtn2userlayout(FRONT_BUTTON preset);
static SCREEN_LAYOUT_MODE ScreenCh2User(SCREEN_LAYOUT_MODE preset);
static SCREEN_LAYOUT_MODE ScreenUser2Ch(SCREEN_LAYOUT_MODE preset);


static SCREEN_LAYOUT_MODE getCurrentLayout(void);

static int createScreen(SCREEN_LAYOUT_MODE layout,HUINT32 width,HUINT32 height);
static int updateScreen(SCREEN_LAYOUT_MODE layout,BOOL force,BOOL savelayoutflag);
static int updateVideo(int devId);
static int prepareVideo(void);
static int deleteAllScreen(void);
static int absoluteX2Relative(int x);
static int absoluteY2Relative(int y);
static int absoluteW2Relative(int w);
static int absoluteH2Relative(int h);
static int relativeX2Absolute(int x);
static int relativeY2Absoulte(int y);
static int relativeW2Absolute(int w);
static int relativeH2Absolute(int h);

static void getCurrentScreenSize(int *pHsize,int *pVsize);

static void SaveData();
static void loadData();
static void SetDefaultData();

static int sendWholeInfoToGUI(void);
static int sendSignalInfoToGUI(void);
static int sendRequestChangeRes(UI32 res);


static int screenFormat2Pannel(SCREEN_FORMAT format,OUT_PANNEL_INFO *pInfo);
static SCREEN_FORMAT check_outformat(int w, int h, int fps);

static int inputscreenFormat2Pannel(SCREEN_LAYOUT_MODE layout);
static BOOL getGUIControlMode(void);
static void setGUIControlMode(BOOL enable);
static int sendRequestChangeLayout(UI32 layout);
static int sendRequestChangeMoveWin(UI8 win, UI8 *pdata);
static int sendRequestChangeResizeWin(UI8 win, UI8 *pdata);
static int sendRequestWinChange(UI8 win, UI8 *pdata);
static int sendRequestChangeApplyInfo(UI8 *pdata);
static int sendRequestSaveUserData();
static BOOL isUserLayout(SCREEN_LAYOUT_MODE mode);
static BOOL isChannelButton(FRONT_BUTTON btn);
static int button2device(FRONT_BUTTON btn);
static VIDEO_SEL_INPUT getNextInputSource(VIDEO_SEL_INPUT input);


void scalePanelData();
int check_bandwidth();
int chgOutres2Guival(int val);

static int videoAllStop(void);



void InitAppl(void)
{
	APP_CONTROL_BLOCK *appcb = &appControlblock;

	APPL_DBG("InitAppl....\r\n");
	InitEvent();
	InitVideo();
	InitNetwork();
	InitFront();

	if(prepareVideo() == 0)
	{
		VK_TaskCreate(app_work_thread,10,512,"app",NULL,&appcb->resource.hWorkThread);
	}
}

static int prepareVideo(void)
{
	APP_CONTROL_BLOCK *appcb = &appControlblock;
	VIDEO_CONTROL_INFO *info = &appControlblock.data.info;
	HUINT32 scr_w, scr_h;
	int layout, devId;

	loadData();

	appcb->enableGUIControl = FALSE;
	screenFormat2Pannel(appControlblock.data.info.outFormat, &appcb->outpannel);

	appcb->outpannel.beforeW = appcb->outpannel.width;
	appcb->outpannel.beforeH = appcb->outpannel.height;

	VideoSetOpMode(VIDEO_OP_MULTIVIEW, appcb->outpannel.width, appcb->outpannel.height);

	for(devId=0; devId<MAX_4K_MSV_INPUT; devId++)
	{
		appcb->txdev[devId].deviceState = REMOVED;
		appcb->txdev[devId].signalState = SIGNALOFF;
		appcb->txdev[devId].isDivided = FALSE;
		appcb->txdev[devId].isStarted = FALSE;

		appcb->txdev[devId].videoHandle = VideoCreatePath(devId,devId);

		appcb->txdev[devId].inputSource = appcb->data.info.inputSource_seltype[devId];
		VideoSetInputSource(appcb->txdev[devId].videoHandle, appcb->txdev[devId].inputSource);
	}
}

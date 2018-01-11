#include <stdio.h>
#include <string.h>

#include "ddi.h"
#include "dcm.h"
#include "4k_msv.h"
#include "video.h"
#include "network.h"
#include "event.h"
#include "util.h"
#include "shell.h"

#define APP_NAME "APP:NET"

#define USE_MANAGER 0

#define BUFF_SIZE	1024+32
#define NETWORK_KEEPALIVE_TIMEOUT 30000



typedef enum 
{
	NETWORK_SERVER_DISCONNECTED,
	NETWORK_SERVER_AUTHORIZATION,
	NETWORK_SERVER_CONNECTED,
} NETWORK_SERVER_STATE;



#define APPL_DBG(FORMAT, ...) DDI_UART_Print("[%-10s]" FORMAT, APP_NAME, ##__VA_ARGS__);
#define APPL_PRINT(FORMAT, ...) DDI_UART_Print(FORMAT, ##__VA_ARGS__);


typedef struct 
{
	BOOL connected;
	BOOL started;
	DDI_HANDLE udpServer;
	VK_HANDLE hTask;
	VK_HANDLE hKeepAliveTmr;
	DDI_NET_CLIENT client;

	UI8 rxbuffer[BUFF_SIZE];
	UI32 rxLength;
	BOOL keepAliveTimerStart;
	NETWORK_SERVER_STATE state;
} NETWORK_SERVER;




typedef struct 
{
	DDI_HANDLE hNet[NETWORK_PORT_END];
	DDI_NET_INFO netInfo[NETWORK_PORT_END];	
} NETWORK_CONTROL_BLOCK;


SI3US-205035-36448


static NETWORK_CONTROL_BLOCK netCBlock;
static NETWORK_SERVER udpServer;




static UI8 md5Hash[] = "b5b3d658b5564861ba11c6f27899b372";
static UI8 AckPacket[] = {'A', 'C', 'K'};
static UI8 NckPacket[] = { 'N', 'A', 'K'};



static int Connection_Flag = 0;
UI32 ChkConTmrId = 0;

static BOOL network_packet_parser(NETWORK_SERVER *pSvr, DDI_NET_CLIENT *pClient);
static BOOL net_checksum(UI8 *Rdata, UI16 payload_len);

static void network_send_event(HUINT32 netEvent, HUINT8 *data, HUINT32 dataLen);

static void serverThread(void *p);
static void network_keepalive_timer_stop(NETWORK_SERVER *pSvr);
static void network_keepalive_timer_start(NETWORK_SERVER *pSvr);



void InitNetwork(void)
{
	NETWORK_CONTROL_BLOCK *ncb = NULL;
	DDI_RESULT_CODE result;
	DDI_UDP_INFO udpdata;
	DDI_NET_INFO netdata;
	int nr;

	ncb = (NETWORK_CONTROL_BLOCK *)&netCBlock;

	for(nr=0; nr<NETWORK_PORT_END; nr++)
	{
		result = DDI_NET_Open(nr, &ncb->hNet[nr]);
		if(result == DDI_OK)
		{
			result = DDI_NET_GetInfo(ncb->hNet[nr], &ncb->netInfo[nr]);
			#if (0)
            APPL_DBG("NETWORK %d\r\n",nr);
    		APPL_DBG("ip address : %s\r\n",ncb->netInfo[nr].ipAddress );
    		APPL_DBG("subnet mask: %s\r\n",ncb->netInfo[nr].netmask);
    		APPL_DBG("gateway    : %s\r\n",ncb->netInfo[nr].gatewayip);
    		APPL_DBG("mac address: %s\r\n",ncb->netInfo[nr].macAddress);
    		#endif
		}
	}

	DCM_NVRAM_Read_Net((void*)&udpdata, sizeof(DDI_UDP_INFO));
	APPL_DBG("----- Read Net Info -----\r\n");
	APPL_DBG("ip = %s\r\n", udpdata.ipAddress);
	APPL_DBG("mask = %s\r\n", udpdata.netmask);
	APPL_DBG("gw = %s\r\n", udpdata.gatewayip);
	APPL_DBG("port = %d\r\n", udpdata.port);

	if(checkValidIpAddr(udp.ipAddress) == FALSE)
	{
		NetworkSetDefault();
	}

	memcpy(&netdata, &udpdata, sizeof(DDI_NET_INFO));
	DDI_NET_Setup(ncb->hNet[0], &netdata, FALSE);
	NetWorkCreateServer(updata.port);
}

int NetworkSetDefault(void)
{
	DDI_UDP_INFO udpdata;
	APPL_DBG("set Network defalut\r\n");

	sprintf(udpdata.ipAddress, "192.168.100.123");
	sprintf(udpdata.ipAddress, "255.255.255.0");
	sprintf(udpdata.ipAddress, "192.168.100.1");
	udpdata.port = 9090;
	DCM_NVRAM_Write_Net((void*)&udpdata, sizeof(DDI_UDP_INFO));

	return(0);
}

int NetworkSetConfig(void *pconfig)
{
	NETWORK_SERVER *pSvr = &udpServer;
	NETWORK_CONTROL_BLOCK *ncb = NULL;
	DDI_UDP_INFO *p = (DDI_UDP_INFO*)pconfig;
	DDI_NET_INFO netdata;

	ncb = (NETWORK_CONTROL_BLOCK*)&netCBlock;

	if(pSvr->connected == TRUE)
	{
		network_send_event(NETWORK_EVENT_DISCONNECT, NULL, 0);
	}

	pSvr->connected = FALSE;
	NetworkDeleteServer();

	memcpy(&netdata, p, sizeof(DDI_NET_INFO));
	DDI_NET_Setup(ncb->hNet[0], &netdata, FALSE);

	NetWorkCreateServer(p->port);
	DCM_NVRAM_Write_Net((void*)p, sizeof(DDI_UDP_INFO));

	network_keepalive_timer_stop(pSvr);

	return 0;
}

int NetworkGetConfig(void *pconfig)
{
	DDI_UDP_INFO *p = (DDI_UDP_INFO *)pconfig;

	if(p != NULL)
	{
		DCM_NVRAM_Read_Net((void*)p, sizeof(DDI_UDP_INFO));
	}

	return (0);
}

int NetWorkCreateServer(UI32 portNumber)
{
	NETWORK_SERVER *pSvr = &udpServer;
	DDI_NET_SERVER_DATA server;

	pSvr->connected = FALSE;
	pSvr->started = FALSE;

	server.portNumber = portNumber;

	if(DDI_NET_CreatUdpServer(&server, &pSvr->udpServer) == DDI_OK)
	{
		APPL_DBG("Create UDP Server OK!! \r\n");
		VK_TaskCreate(serverThread, 10, 4096, "server", NULL, &pSvr->hTask);
	}

	return (0);
}

int NetworkRestartServer()
{
	NETWORK_SERVER *pSvr = &udpServer;
}

int NetworkDeleteServer(void)
{
	NETWORK_SERVER *pSvr = &udpServer;

#if 1
	if(pSvr->connected)
	{
		DDI_NET_Close(pSvr->udpServer);
	}
#else
	if(pSvr->connected)
	{
		DDI_NET_DeleteUdpServer(&pSvr->udpServer);
	}
#endif

	return (0);

}

static void serverThread(void *p)
{
	DDI_NET_CLIENT client;
	NETWORK_SERVER *pSvr = &udpServer;
	UI32 r;

	APPL_DBG("start server!\r\n");

	pSvr->started = TRUE;

	while(TRUE)
	{
		pSvr->rxLength = DDI_NET_Read(pSvr->udpServer, pSvr->rxbuffer, BUFF_SIZE, &client);
		if(pSvr->rxLength > 0)
		{
			if(network_packet_parser(pSvr, &client) == TRUE)
			{
				DDI_NET_Write(pSvr->udpServer, AckPacket, sizeof(AckPacket), &client);
			}
			else 
			{
				DDI_NET_Write(pSvr->udpServer, NckPacket, sizeof(NckPacket), &client);
			}
		}

		VK_TaskSleep(5);
	}
}

int NetworkSendTo(UI8 Cmd, UI8 Locate, UI8 *Payload_data, UI16 Value_Len)
{
	UI8 buf[1024];
	UI8 *SendBuf;
	UI16 Payload_Len = Value_Len + 4;
	UI16 SendLen = Payload_Len + 5;
	NETWORK_SERVER *pSvr = &udpServer;

	SendBuf = buf;

	SendBuf[HTYPE] = HEADER_TYPE_WHOLE_INFO_RES;
	SendBuf[HLEN_MSB] = ((Payload_Len) >> 8) & 0x00ff;
	SendBuf[HLEN_LSB] = (Payload_Len) & 0x00ff;
	SendBuf[HCLVNUM] = 1;
	SendBuf[CCMD] = Cmd;
	SendBuf[CLOCATION] = Locate;
	SendBuf[CLEN_MSB] = (Value_Len >> 8) & 0x00ff;
	SendBuf[CLEN_LSB] = Value_Len & 0x00ff;

	memcpy(&SendBuf[CVALUE_START], Payload_Data, Value_Len);

	SendBuf[CVALUE_START + Value_Len] = net_checksum(&SendBuf[HPAYLOAD_START], Payload_Len);

	return (DDI_NET_Write(pSvr->udpServer, SendBuf, SendLen, &pSvr->client) == SendLen) ? 0 : -1;
}

static void network_keepalive_timer_handler(UI32 tmr, void *param)
{
	NETWORK_SERVER *pSvr = &udpServer;

	pSvr->connected = FALSE;
	pSvr->keepAliveTimerStart = FALSE;

	APPL_DBG(COL_RED"Timeout Keep Alive!."COL_RESET "\r\n");
	network_send_event(NETWORK_EVENT_DISCONNECT, NULL, 0);
}

static void network_keepalive_timer_start(NETWORK_SERVER *pSvr)
{
	if(pSvr->keepAliveTimerStart == TRUE)
	{
		VK_TimerCancel(pSvr->hKeepAliveTmr);
		pSvr->keepAliveTimerStart = FALSE;
	}
	VK_TimerEventAfter(NETWORK_KEEPALIVE_TIMEOUT,
						network_keepalive_timer_handler,
						NULL, 0 , &pSvr->hKeepAliveTmr);
	pSvr->keepAliveTimerStart = TRUE;
}

static void network_keepalive_timer_stop(NETWORK_SERVER *pSvr)
{
	if(pSvr->keepAliveTimerStart == TRUE)
	{
		VK_TimerCancel(pSvr->hKeepAliveTmr);
	}

	pSvr->keepAliveTimerStart = FALSE;
}

static BOOL net_checksum(UI8 *Rdata, UI16 payload_len)
{
	int i;
	UI8 chk_sum = 0;
	for(i=0; i<payload_len; i++)
	{
		chk_sum += *Rdata++;
	}
	if( *Rdata == (0xff - chk_sum))
	{
		return TRUE;
	}

	return TRUE;
}

static BOOL network_packet_parser(NETWORK_SERVER *pSvr, DDI_NET_CLIENT *pClient)
{
	UI8 md5str[32], tmp[16], temp[4];
	int i;

	UI16 payloadLen;
	UI8 clvLen;

	memset(md5str, 0, sizeof(md5str));

	if(((HEADER_TYPE_REQ_START < pSvr->rxbuffer[HTYPE]) && (HEADER_TYPE_REQ_MAX > pSvr->rxbuffer[HTYPE])) ||
		((HEADER_TYPE_RES_START < pSvr->rxbuffer[HTYPE]) && (HEADER_TYPE_RES_MAX > pSvr->rxbuffer[HTYPE])))
	{
		payloadLen = pSvr->rxbuffer[HLEN_MSB];
		payloadLen = (payloadLen << 8) | pSvr->rxbuffer[HLEN_LSB];
		clvLen = pSvr->rxbuffer[HCLVNUM];

		if( TRUE == net_checksum(&pSvr->rxbuffer[HPAYLOAD_START], payloadLen))
		{
			switch(pSvr->rxbuffer[HTYPE])
			{
				case HEADER_TYPE_CONNECT_REQ :
				{
					APPL_DBG("-----------------------HEADER_TYPE_CONNECT_REQ----------------------\r\n");
					if( pSvr->connected == FALSE)
					{
						for(i=0; i<16; i++)
						{
							sprintf(temp, "%02x", pSvr->rxbuffer[(HTYPE+1)]+i);
							strcat(md5str, tmp);
						}
						if(strcmp(md5str, md5Hash) =! 0)
						{
							APPL_DBG("connected refuse!\r\n");
							return FALSE;
						}
						else 
						{
							APPL_DBG(COL_GREEN"CONNECTED %s:%d"COL_RESET "\r\n", DDI_NET_Addr2String(pClient->ipaddr), pClient->portNumber);
							pSvr->client.ipaddr = pClient->ipaddr;
							pSvr->client.portNumber = pClient->portNumber;
							pSvr->connected = TRUE;
							network_keepalive_timer_start(pSvr);
							network_send_event(NETWORK_SERVER_CONNECT, NULL, 0);
							return TRUE; 
						}
					}
					else
					{
						APPL_DBG(COL_GREEN"already connection! %s"COL_RESET "\r\n", DDI_NET_Addr2String(pSvr->client.ipaddr));
						return FALSE;
					}

					break;
				}
				case HEADER_TYPE_WHOLE_INFO_REQ :
				{
					APPL_DBG("-----------------------HEADER_TYPE_WHOLE_INFO_REQ----------------------\r\n");
					if(pSvr->connected == TRUE)
					{
						network_send_event(NETWORK_EVENT_GET_INFO,NULL,0);
					}

					break;
				}
				case HEADER_TYPE_EVENT_REQ :
				{
					APPL_DBG("-----------------------HEADER_TYPE_EVENT_REQ----------------------\r\n");
					if(pSvr->connected == TRUE)
					{
						network_send_event(NETWORK_EVENT_SET_INFO, &pSvr->rxbuffer[HPAYLOAD_START], payloadLen);
					}
					break;
				}
				case HEADER_TYPE_DISCONNECT_REQ :
				{
					APPL_DBG("-----------------------HEADER_TYPE_DISCONNECT_REQ----------------------\r\n");
					pSvr->connected = FALSE;
					network_keepalive_timer_stop(pSvr);
					network_send_event(NETWORK_EVENT_DISCONNECT,NULL,0);

					break;
				}
				case HEADER_TYPE_CHKCONNECTION_REQ :
				{
					APPL_DBG("-----------------------HEADER_TYPE_CHKCONNECTION_REQ----------------------\r\n");

					if(pSvr->connected == TRUE)
					{
						network_keepalive_timer_start(pSvr);
					}
					break;
				}
				case HEADER_TYPE_INFO_SET_REQ :
				{
					APPL_DBG("-----------------------HEADER_TYPE_INFO_SET_REQ----------------------\r\n");
					if(pSvr->connected == TRUE)
					{
						DDI_UDP_INFO udpdata, readudpdata;
						DDI_NET_INFO netdata;
						UI8 bytes[14];

						memcpy(&bytes, &pSvr->rxbuffer[CVALUE_START], sizeof(UI8)*14);

						sprintf(udpdata.ipAddress, "%d.%d.%d.%d", bytes[0],bytes[1],bytes[2],bytes[3]);
						sprintf(udpdata.netmask, "%d.%d.%d.%d", bytes[4],bytes[5],bytes[6],bytes[7]);
						sprintf(udpdata.gatewayip, "%d.%d.%d.%d", bytes[8],bytes[9],bytes[10],bytes[11]);

						APPL_DBG("------- change net info ------\r\n");
						APPL_DBG("ip 	 = %s\r\n", udpdata.ipAddress);
						APPL_DBG("netmask = %s\r\n", udpdata.netmask);
						APPL_DBG("gw 	 = %s\r\n", udpdata.gatewayip);

						NetworkDeleteServer();
						pSvr->connected = FALSE;

						DDI_NET_Setup(0, &netdata, FALSE);

						NetWorkCreateServer(udpdata.port);

						DCM_NVRAM_Write_Net((void*)&udpdata, sizeof(DDI_UDP_INFO));

						VK_TaskSleep(500);
						network_keepalive_timer_stop(pSvr);

						APPL_DBG("Ethernet Connection Refuse...\r\n");
					}

					break;
				}
			}
			return (pSvr->connected == TRUE) ? TRUE : FALSE;
		}
	}
	return FALSE;
}

static void network_send_event(HUINT32 netEvent, HUINT8 *data, HUINT32 dataLen)
{
	NETWORK_MSG *pmsg = NULL;
	VK_EVENT event;
	event.event_type = EVENT_STATUS_NETWORK;
	event.event_data = netEvent;

	if((dataLen > 0) && (data != NULL))
	{
		pmsg = VK_Alloc(sizeof(NETWORK_MSG));
		pmsg->length = dataLen;
		memcpy(&pmsg->data[0], &data[0], dataLen);
	}

	event.data32 = dataLen;
	event.pvdata = pmsg;
	event.after_free = (pmsg == NULL) ? FALSE : TRUE;
	VK_EventSend(EVENT_STATUS_NETWORK, EVENT_CLASS_NETWORK, &event);
}

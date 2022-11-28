/**********************************************************************************************************************************/
#ifndef ASIM_H
#define ASIM_H

// Configs
#include <Arduino.h>

#ifdef ARDUINO
	#if (ARDUINO >= 100)
	#include <Arduino.h>
	#else
	#include <WProgram.h>
	#include <pins_arduino.h>
	#include <NewSoftSerial.h>
	#endif
#endif

#if (defined(__AVR__))
	#include <avr/pgmspace.h>
#else 
	#include <pgmspace.h>
#endif

#ifndef prog_char_strcmp
	#define prog_char_strcmp(a, b) strcmp((a), (b))
#endif

#ifndef prog_char_strstr
	#define prog_char_strstr(a, b) strstr((a), (b))
#endif

#ifndef prog_char_strlen
	#define prog_char_strlen(a) strlen((a))
#endif

#ifndef prog_char_strcpy
	#define prog_char_strcpy(to, fromprogmem) strcpy((to), (fromprogmem))
#endif

#define SHOW_SIM_DEBUG

// for debug (only applies when SHOW_SIM_DEBUG is defined)
#ifdef SHOW_SIM_DEBUG
	// DebugStream	sets the Stream output to use
	#define DebugStream 		Serial
	// need to do some debugging...
	#define DEBUG_PRINT(...) 	DebugStream.print(__VA_ARGS__)
	#define DEBUG_PRINTLN(...) 	DebugStream.println(__VA_ARGS__)
#else
	#define DEBUG_PRINT(...) 
	#define DEBUG_PRINTLN(...)
#endif
/**********************************************************************************************************************************/
// defines to keep things readable
#define SIM_OK				1
#define SIM_FAILED			0

#define SIM800				2
#define SIM808_V1 			7
#define SIM808_V2 			14
#define UNKNOWN_TYPE		255

#define MCI					70
#define	IRANCELL			80
#define	RITEL				90
#define VODAFONE			100
#define ATandT				110
#define	ETISALAT			120
#define UNKNOWN_SIM			250

#define WEAK_SIGNAL			0
#define MARGINAL_SIGNAL		1
#define GOOD_SIGNAL			2
#define EXCELLENT_SIGANL	3 

#define TEXT_MODE			1
#define PDU_MODE			0 

#define FARSI				27
#define ENGLISH				37

#define IP_INITIAL			0
#define IP_START			1
#define IP_CONFIG			2
#define IP_GPRSACT			3
#define IP_STATUS			4
#define TCP_CONNECTING		5
#define TCP_CONNECTED		6
#define TCP_CLOSING			7
#define TCP_CLOSED			8
#define PDP_DEACTIVATED		9
// Configs, Feel free to change them according to your project
// #define FUll_CONFIG
#define DEFAULT_TIMOUT 		100
#define DEFUALT_INIT_WAIT	6000
#define DEFUALT_MODE		TEXT_MODE
#define DEFUALT_CHARSET		"GSM"
#define SET_SMS_PARAM
// #define SET_LANG_TO_ENG

// a few typedefs to keep things portable
typedef Stream ASIMStreamType;
typedef const __FlashStringHelper *ASIMFlashString;

// public buffer to store replies
/**********************************************************************************************************************************/
class ASIM {
	public:
		// Basic
		ASIM(byte in_pwr, byte pwr_key, byte rst);
		bool begin(ASIMStreamType &port, int setup_wait);
		// Stream
		int available(void);
		size_t write(uint8_t x);
		int read(void);
		size_t readBytes(char * buffer, uint16_t sizeOfBuffer);
		int peek(void);
		void flush();
		// Send command and verify reply
		bool sendVerifyedCommand(char *send, char *reply, uint16_t timeout = DEFAULT_TIMOUT);
		bool sendVerifyedCommand(ASIMFlashString send, ASIMFlashString reply, uint16_t timeout = DEFAULT_TIMOUT);
  		bool sendVerifyedCommand(char *send, ASIMFlashString reply, uint16_t timeout = DEFAULT_TIMOUT);
		bool parseReplyQuoted(char *buffer, ASIMFlashString toreply, char *v, int maxlen, char divider, uint8_t index);
		// Modem information
		uint8_t getModemType();
		int8_t getSimType();
		uint8_t getIMEI();
		// Modem status
		bool checkregistration();
		bool checkPIN();
		int8_t getSignalQuality();
		// Setting
		bool checkConnection(ASIMFlashString reply);
		bool echoOff();
		bool setBaud(unsigned long baud);
		bool setFunctionality(uint8_t mode);
		bool setMessageFormat(uint8_t format);
		bool setCharSet(char *chs);
		bool setCallerIdNotification();
		bool setSMSParameters(uint8_t fo, uint16_t vp, uint8_t pid, uint8_t dcs);
		bool setSIMLanguage(uint8_t lang);
		bool softReset();
		bool hardReset();
		// Calls
		bool makeCall(char *number);
		bool hangUp();
		bool makeMissedCall(char *number, uint16_t hangup_delay);
		bool makeAMRVoiceCall(char *number, uint16_t file_id);
		bool incomeCallNumber(char *phone_number);
		bool incomeCallNumber(char *phone_number, char* buff);
		// SMS
		bool clearInbox();
		bool deleteSMS(uint8_t message_index);
		bool sendSMS(char *receiver_number, char *msg);
		bool readSMS(uint8_t message_index, char *sender, char *body, uint16_t *sms_len, uint16_t maxlen);
		bool readSMS(uint8_t message_index, char *sender, char *body, char *date, char *tyme, char *type, uint16_t *sms_len, uint16_t maxlen);
		int8_t getNumSMS();
		// USSD
		bool sendUSSD(char *ussd_code, char *ussd_response, uint16_t *response_len, uint16_t max_len);
		// GPRS handling
		bool enableGPRS();
		bool disableGPRS();
		bool getGPRSLocation(uint16_t *error, float *lat, float *lon);
		// HTTP
		bool initHttp();
		bool termHttp();
		bool setHttpParameter(ASIMFlashString parameter, const char *value);
		bool setHttpParameter(ASIMFlashString parameter, ASIMFlashString value);
		bool setHttpParameter(ASIMFlashString parameter, int32_t value);
		bool setHttpDataParameter(uint32_t size, uint32_t max_wait);
		bool setHttpAction(uint8_t method, uint16_t *status, uint16_t *data_len, int32_t timeout);
		bool readHttpResponse(uint16_t *data_len);
		bool postHttpRequest(char *url, ASIMFlashString cont_type, const uint8_t *data, uint16_t data_len, uint16_t *status, uint16_t *response_len);
		// TCP/IP connection
		uint8_t getTCPStatus();
		bool establishTCP();
		bool startTCP(char *server, uint16_t port);
		bool closeTCP();
		bool sendTCPData(char *data, char *response);
		// RTC
		bool initRTC(uint8_t mode);
		bool setRTC(uint8_t year, uint8_t month, uint8_t day, uint8_t hr, uint8_t min, uint8_t sec, int8_t zz);
		bool readRTC(uint8_t *year, uint8_t *month, uint8_t *day, uint8_t *hr, uint8_t *min, uint8_t *sec);
  		bool syncNTPTime(uint16_t *error_code, char *ntp_server, uint8_t region);
		// PWM
		bool setPWM(uint8_t channel, uint16_t period, uint8_t duty);
		// Vars
		ASIMStreamType *simSerial;
		uint8_t _sim_type = UNKNOWN_SIM;
		char _modem_ip[15];
	private:
		// Stream
		void flushInput();
		uint8_t readAnswer(uint16_t timeout = DEFAULT_TIMOUT, bool multiline = false);
		uint8_t readAnswerLn(uint16_t timeout = DEFAULT_TIMOUT, bool multiline = false);
		// Send command and verify reply
		bool sendVerifyedCommand(ASIMFlashString prefix, char *suffix, ASIMFlashString reply, uint16_t timeout = DEFAULT_TIMOUT);
		bool sendVerifyedCommand(ASIMFlashString prefix, int32_t suffix, ASIMFlashString reply, uint16_t timeout = DEFAULT_TIMOUT);
		bool sendVerifyedCommand(ASIMFlashString prefix, int32_t suffix, int32_t suffix2, ASIMFlashString reply, uint16_t timeout = DEFAULT_TIMOUT);
		bool sendVerifyedCommandQuoted(ASIMFlashString prefix, ASIMFlashString suffix, ASIMFlashString reply, uint16_t timeout = DEFAULT_TIMOUT);
		// Get and parse reply from GSM
		uint8_t getReply(char *send, uint16_t timeout = DEFAULT_TIMOUT);
		uint8_t getReply(ASIMFlashString send, uint16_t timeout = DEFAULT_TIMOUT);
		uint8_t getReply(ASIMFlashString prefix, char *suffix, uint16_t timeout = DEFAULT_TIMOUT);
		uint8_t getReply(ASIMFlashString prefix, int32_t suffix, uint16_t timeout = DEFAULT_TIMOUT);
		uint8_t getReply(ASIMFlashString prefix, int32_t suffix1, int32_t suffix2,uint16_t timeout); 
		uint8_t getReplyQuoted(ASIMFlashString prefix, ASIMFlashString suffix,uint16_t timeout = DEFAULT_TIMOUT);
		bool sendParseReply(ASIMFlashString tosend, ASIMFlashString toreply, uint16_t *v, char divider = ',', uint8_t index = 0);
		bool parseReply(ASIMFlashString toreply, uint16_t *v, char divider = ',', uint8_t index = 0);
  		bool parseReply(ASIMFlashString toreply, char *v, char divider = ',', uint8_t index = 0);
		// Vars
		ASIMFlashString ok_reply; 
		uint8_t _modem_type = 0;
		uint8_t _tcp_status = IP_INITIAL;
		byte _in_pwr_pin;
		byte _pwr_key_pin;
		byte _rst_pin;
		char _imei[20];
		bool _incoming_call = false;
		bool _gprs_on = false;
		bool _tcp_running = false;
};
/**********************************************************************************************************************************/		  
#endif
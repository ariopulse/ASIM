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
#define	RTEL				90
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
// Configs, Feel free to change them according to your project
#define DEFAULT_TIMOUT 		1000
#define DEFUALT_INIT_WAIT	3000
#define DEFUALT_MODE		TEXT_MODE
#define DEFUALT_CHARSET		"GSM"
#define SET_SMS_PARAM
#define SET_LANG_TO_ENG

// a few typedefs to keep things portable
typedef Stream ASIMStreamType;
typedef const __FlashStringHelper *ASIMFlashString;

// public buffer to store replies
/**********************************************************************************************************************************/
class ASIM {
	public:
		// Basic
		ASIM(byte in_pwr, byte pwr_key, byte rst);
		bool begin(ASIMStreamType &port);
		// Stream
		int available(void);
		size_t write(uint8_t x);
		int read(void);
		int peek(void);
		void flush();
		// Send command and verify reply
		bool sendVerifyedCommand(char *send, char *reply, uint16_t timeout = DEFAULT_TIMOUT);
		bool sendVerifyedCommand(ASIMFlashString send, ASIMFlashString reply, uint16_t timeout = DEFAULT_TIMOUT);
  		bool sendVerifyedCommand(char *send, ASIMFlashString reply, uint16_t timeout = DEFAULT_TIMOUT);
  		bool expectedReply(ASIMFlashString reply, uint16_t timeout = DEFAULT_TIMOUT);
		// Modem information
		uint8_t getModemType();
		uint8_t getSimType();
		// Modem status
		bool checkModemStatus();
		bool checkregistration();
		bool checkPIN();
		uint8_t getSignalQuality();
		// Set parameters
		bool setBaud(unsigned long baud);
		bool setMessageFormat(uint8_t format);
		bool setCharSet(char *chs);
		bool setCLI();
		bool setSMSParameters(uint8_t fo, uint16_t vp, uint8_t pid, uint8_t dcs);
		bool setSIMLanguage(uint8_t lang);
		// Calls
		bool makeCall(char *number);
		bool hangUp();
		bool makeMissedCall(char *number, uint16_t hangup_delay);
		// SMS
		bool clearInbox();
		bool deleteSMS(uint8_t message_index);
		bool sendSMS(char *receiver_number, char *msg);
		bool readSMS(uint8_t message_index, char *sender, char *body, uint16_t *sms_len, uint16_t maxlen);
		bool readSMS(uint8_t message_index, char *sender, char *body, char *date, char *tyme, uint16_t *sms_len, uint16_t maxlen);
		int8_t getNumSMS();
		// USSD
		bool sendUSSD(char *ussd_code, char *ussd_response, uint16_t *response_len, uint16_t max_len);
		// TCP/IP connection
		bool establishTCP();
		bool startTCP(char *server, uint16_t port);
		bool closeTCP();
		bool getTCPStatus();
		bool sendTCPData(char *data, uint16_t len);
  		// Vars
		ASIMStreamType *simSerial;

		uint8_t _sim_type = UNKNOWN_SIM;
		char _modem_ip[15];
	private:
		// Stream
		void flushInput();
		uint16_t readRaw(uint16_t read_length);
		uint8_t readAnswer(uint16_t timeout = DEFAULT_TIMOUT, bool multiline = false);
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
		bool parseReply(ASIMFlashString toreply, uint16_t *v, char divider = ',', uint8_t index = 0);
  		bool parseReply(ASIMFlashString toreply, char *v, char divider = ',', uint8_t index = 0);
  		bool parseReplyQuoted(ASIMFlashString toreply, char *v, int maxlen, char divider, uint8_t index);
  		bool sendParseReply(ASIMFlashString tosend, ASIMFlashString toreply, uint16_t *v, char divider = ',', uint8_t index = 0);
		// Modem information
		uint8_t getIMEI();
		// Vars
		byte _in_pwr_pin;
		byte _pwr_key_pin;
		byte _rst_pin;

		ASIMFlashString ok_reply; 

		uint8_t _modem_type = 0;
		char _imei[20];
};
/**********************************************************************************************************************************/		  
#endif

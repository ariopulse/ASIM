/**********************************************************************************************************************************/
#include "SIM.h"

char replybuffer[255];
/**********************************************************************************************************************************/
/**
 * @brief Construct a new Ario_SIM object
 *
 * @param in_pwr The 4.1v regulator enable pin
 * @param pwr_key The power key input pin in GSM
 * @param rst The reset pin of GSM
 * @param display_msg Show debugging report on serial port
*/
ASIM::ASIM(byte in_pwr, byte pwr_key, byte rst) {
	_in_pwr_pin = in_pwr;
	_pwr_key_pin = pwr_key;
	_rst_pin = rst;

	simSerial = 0;

	ok_reply = F("OK");
}

/**
 * @brief Connect to the cell module
 *
 * @param port the serial connection to use to connect
 * @return bool true on success, false if a connection cannot be made
*/
bool ASIM::begin(Stream &port) {
	simSerial = &port;

	if(_in_pwr_pin > 0) {
		pinMode(_in_pwr_pin, OUTPUT);
		digitalWrite(_in_pwr_pin, HIGH);
	}

 	if(_rst_pin > 0) {
		pinMode(_rst_pin, OUTPUT);
		digitalWrite(_rst_pin, HIGH);
		delay(100);
	}

 	if(_pwr_key_pin > 0) {
		pinMode(_pwr_key_pin, OUTPUT);
		digitalWrite(_pwr_key_pin, HIGH);
		delay(1000);
		digitalWrite(_pwr_key_pin, LOW);
	}

	delay(DEFUALT_INIT_WAIT);
	DEBUG_PRINTLN(F("================= ESTABLIS COMMUNICATON ================="));
	DEBUG_PRINTLN(F("Try communicate with modem (May take 10 seconds to find cellular network)"));

	// give 7 seconds to reboot
	uint16_t timeout = DEFUALT_INIT_WAIT;

	while (timeout > 0) {
		while (simSerial->available())
		 	simSerial->read();
		if (sendVerifyedCommand(F("AT"), ok_reply, 500))
			break;
		while (simSerial->available())
			simSerial->read();
		if (sendVerifyedCommand(F("AT"), F("AT"), 500))
		  	break;
		delay(500);
		timeout -= 500;
	}

	if(timeout <= 0) {
		DEBUG_PRINTLN("Timeout: No response to AT... last attempt.");
		sendVerifyedCommand(F("AT"), ok_reply, 500);
		delay(1000);
	}

	if(!sendVerifyedCommand(F("AT"), ok_reply, 500)) {
		return SIM_FAILED;
	}

	DEBUG_PRINTLN(F("================= INIT MODEM ================="));
	DEBUG_PRINTLN(F("The connection with the modem was successfully established"));
	DEBUG_PRINTLN(F("Initializing....(May take 10 seconds)"));

	// Turn of echo
	sendVerifyedCommand(F("ATE0"), ok_reply);
	delay(100);

	// Get modem type
	_modem_type = getModemType();

	// Get modem IMEI
	getIMEI();

	// Get SIM card type
	_sim_type = getSimType();

	// Check modem status
	checkModemStatus();

	// Set baudrate
	setBaud(9600);

	// Set message mode
	#ifdef DEFUALT_MODE
		setMessageFormat(DEFUALT_MODE);
	#endif

	// Set char set
	#ifdef	DEFUALT_CHARSET
		setCharSet(DEFUALT_CHARSET);
	#endif

	// Set CLI
	setCLI();

	// Set SMS parameters
	# ifdef SET_SMS_PARAM
		setSMSParameters(49, 167, 0, 0);
	#endif

	// Delete all sms
	clearInbox();

	// Flush serial port
	flushInput();


	return SIM_OK;
}
/**********************************************************************************************************************************/
/**
 * @brief Serial data available
 *
 * @return int
*/
inline int ASIM::available(void) { 
	return simSerial->available(); 
} 

/**
 * @brief Serial write
 *
 * @param x
 * @return size_t
*/
inline size_t ASIM::write(uint8_t x) {
	return simSerial->write(x);
} 

/**
 * @brief Serial read
 *
 * @return int
*/
inline int ASIM::read(void) { 
	return simSerial->read(); 
}

/**
 * @brief Serial peek
 *
 * @return int
*/
inline int ASIM::peek(void) { 
	return simSerial->peek(); 
} 

/**
 * @brief Flush the serial data
 *
*/
inline void ASIM::flush() { 
	simSerial->flush(); 
} 

/**
 * @brief Read all available serial input to flush pending data.
 *
*/
void ASIM::flushInput() {
	uint16_t timeoutloop = 0;
	while (timeoutloop++ < 40) {
		while (available()) {
			read();
			timeoutloop = 0; // If char was received reset the timer
		}
		delay(1);
	}
}

/**
 * @brief Read what comse from serial port up to 254 bytes or desired length
 *
 * @param read_length Desired length for read
 * @return uint16_t the length of the incoming message
*/
uint16_t ASIM::readRaw(uint16_t read_length) {
	uint16_t idx = 0;
	while (read_length && (idx < sizeof(replybuffer) - 1)) {
		if (simSerial->available()) {
			replybuffer[idx] = simSerial->read();
			idx++;
			read_length--;
		}
	}
	replybuffer[idx] = 0;

	return idx;
}

/**
 * @brief Read a single line or up to 254 bytes
 *
 * @param timeout Reply timeout
 * @param multiline true: read the maximum amount. false: read up to the second
 * newline
 * @return uint8_t the number of bytes read
 */
uint8_t ASIM::readAnswer(uint16_t timeout, bool multiline) {
	uint16_t replyidx = 0;

	while (timeout--) {
		if (replyidx >= 254) {
			break;
		}

		while (simSerial->available()) {
			char c = simSerial->read();

			if ((c == '\r') || (c == '\n')) continue;
			if ((c == 'A') || (c == 'T')) continue;
			if (c == 0xA) {
				if (replyidx == 0) continue; // the first 0x0A is ignored
	
				if (!multiline) {
				  timeout = 0; // the second 0x0A is the end of the line
				  break;
				}
			}
			replybuffer[replyidx] = c;
			replyidx++;
		}

		if (timeout == 0) break;
		delay(1);
	}
	replybuffer[replyidx] = 0; // null term
	return replyidx;
}
/**********************************************************************************************************************************/
/**
 * @brief Send data and verify the response matches an expected response
 *
 * @param send Pointer to the buffer of data to send
 * @param reply Buffer with the expected reply
 * @param timeout Read timeout
 * @return true: success, false: failure
*/
bool ASIM::sendVerifyedCommand(char *send, char *reply, uint16_t timeout) {
	if (!getReply(send, timeout)) {
		return false;
	}

	return (strcmp(replybuffer, reply) == 0);
}

/**
 * @brief Send data and verify the response matches an expected response
 *
 * @param send Pointer to the buffer of data to send
 * @param reply Pointer to a buffer with the expected reply
 * @param timeout Read timeout
 * @return true: success, false: failure
*/
bool ASIM::sendVerifyedCommand(ASIMFlashString send, ASIMFlashString reply, uint16_t timeout) {
	if (!getReply(send, timeout)) {
		return false;
	}

	return (prog_char_strcmp(replybuffer, (prog_char *)reply) == 0);
}

/**
 * @brief Send data and verify the response matches an expected response
 *
 * @param send Pointer to the buffer of data to send
 * @param reply Pointer to a buffer with the expected reply
 * @param timeout Read timeout
 * @return true: success, false: failure
*/
bool ASIM::sendVerifyedCommand(char *send, ASIMFlashString reply, uint16_t timeout) {
	if (!getReply(send, timeout)) {
		return false;
	}
	return (prog_char_strcmp(replybuffer, (prog_char *)reply) == 0);
}

/**
 * @brief Send data and verify the response matches an expected response
 *
 * @param prefix Pointer to a buffer with the prefix send
 * @param suffix Pointer to a buffer with the suffix send
 * @param reply Pointer to a buffer with the expected reply
 * @param timeout Read timeout
 * @return true: success, false: failure
*/
bool ASIM::sendVerifyedCommand(ASIMFlashString prefix, char *suffix, ASIMFlashString reply, uint16_t timeout) {
	getReply(prefix, suffix, timeout);
	return (prog_char_strcmp(replybuffer, (prog_char *)reply) == 0);
}

/**
 * @brief Send data and verify the response matches an expected response
 *
 * @param prefix Pointer to a buffer with the prefix to send
 * @param suffix The suffix to send
 * @param reply Pointer to a buffer with the expected reply
 * @param timeout Read timeout
 * @return true: success, false: failure
*/
bool ASIM::sendVerifyedCommand(ASIMFlashString prefix, int32_t suffix, ASIMFlashString reply, uint16_t timeout) {
	getReply(prefix, suffix, timeout);
	return (prog_char_strcmp(replybuffer, (prog_char *)reply) == 0);
}

/**
 * @brief Send data and verify the response matches an expected response
 *
 * @param prefix Ponter to a buffer with the prefix to send
 * @param suffix1 The first suffix to send
 * @param suffix2 The second suffix to send
 * @param reply Pointer to a buffer with the expected reply
 * @param timeout Read timeout
 * @return true: success, false: failure
*/
bool ASIM::sendVerifyedCommand(ASIMFlashString prefix, int32_t suffix1, int32_t suffix2, ASIMFlashString reply, uint16_t timeout) {
	getReply(prefix, suffix1, suffix2, timeout);
	return (prog_char_strcmp(replybuffer, (prog_char *)reply) == 0);
}

/**
 * @brief Send data and verify the response matches an expected response
 *
 * @param prefix Pointer to a buffer with the prefix to send
 * @param suffix Pointer to a buffer with the suffix to send
 * @param reply Pointer to a buffer with the expected response
 * @param timeout Read timeout
 * @return true: success, false: failure
*/
bool ASIM::sendVerifyedCommandQuoted(ASIMFlashString prefix, ASIMFlashString suffix, ASIMFlashString reply, uint16_t timeout) {
	getReplyQuoted(prefix, suffix, timeout);
	return (prog_char_strcmp(replybuffer, (prog_char *)reply) == 0);
}

/**
 * @brief Send a command and return the reply
 *
 * @param send The char* command to send
 * @param timeout Timeout for reading a  response
 * @return uint8_t The response length
*/
uint8_t ASIM::getReply(char *send, uint16_t timeout) {
	flushInput();

	DEBUG_PRINT(F("\t ---> "));
	DEBUG_PRINTLN(send);

	simSerial->println(send);

	uint8_t l = readAnswer(timeout);

	DEBUG_PRINT("\t");
	DEBUG_PRINT(replybuffer);
	DEBUG_PRINTLN(F(" <--- \n"));
	

	return l;
}

/**
 * @brief Send a command and return the reply
 *
 * @param send The ASIMFlashString command to send
 * @param timeout Timeout for reading a response
 * @return uint8_t The response length
*/
uint8_t ASIM::getReply(ASIMFlashString send, uint16_t timeout) {
	flushInput();

	DEBUG_PRINT(F("\t ---> "));
	DEBUG_PRINTLN(send);

	simSerial->println(send);

	uint8_t l = readAnswer(timeout);

	DEBUG_PRINT("\t");
	DEBUG_PRINT(replybuffer);
	DEBUG_PRINTLN(F(" <--- \n"));

	return l;
}

/**
 * @brief Send a command as prefix and suffix
 *
 * @param prefix Pointer to a buffer with the command prefix
 * @param suffix Pointer to a buffer with the command suffix
 * @param timeout Timeout for reading a response
 * @return uint8_t The response length
*/
uint8_t ASIM::getReply(ASIMFlashString prefix, char *suffix, uint16_t timeout) {
	flushInput();

	DEBUG_PRINT(F("\t ---> "));
	DEBUG_PRINT(prefix);
	DEBUG_PRINTLN(suffix);

	simSerial->print(prefix);
	simSerial->println(suffix);

	uint8_t l = readAnswer(timeout);

	DEBUG_PRINT("\t");
	DEBUG_PRINT(replybuffer);
	DEBUG_PRINTLN(F(" <--- \n"));

	return l;
}

/**
 * @brief Send a command with
 *
 * @param prefix Pointer to a buffer with the command prefix
 * @param suffix The command suffix
 * @param timeout Timeout for reading a response
 * @return uint8_t The response length
*/
uint8_t ASIM::getReply(ASIMFlashString prefix, int32_t suffix, uint16_t timeout) {
	flushInput();

	DEBUG_PRINT(F("\t ---> "));
	DEBUG_PRINT(prefix);
	DEBUG_PRINTLN(suffix, DEC);

	simSerial->print(prefix);
	simSerial->println(suffix, DEC);

	uint8_t l = readAnswer(timeout);

	DEBUG_PRINT("\t");
	DEBUG_PRINT(replybuffer);
	DEBUG_PRINTLN(F(" <--- \n"));

	return l;
}

/**
 * @brief Send command with prefix and two suffixes
 *
 * @param prefix Pointer to a buffer with the command prefix
 * @param suffix1 The comannd first suffix
 * @param suffix2 The command second suffix
 * @param timeout Timeout for reading a response
 * @return uint8_t The response length
*/
uint8_t ASIM::getReply(ASIMFlashString prefix, int32_t suffix1, int32_t suffix2, uint16_t timeout) {
	flushInput();

	DEBUG_PRINT(F("\t ---> "));
	DEBUG_PRINT(prefix);
	DEBUG_PRINT(suffix1);
	DEBUG_PRINT(",");
	DEBUG_PRINTLN(suffix2);

	simSerial->print(prefix);
	simSerial->print(suffix1);
	simSerial->print(',');
	simSerial->println(suffix2, DEC);

	uint8_t l = readAnswer(timeout);

	DEBUG_PRINT("\t");
	DEBUG_PRINT(replybuffer);
	DEBUG_PRINTLN(F(" <--- \n"));

	return l;
}

/**
 * @brief Send command prefix and suffix, returning the response length
 *
 * @param prefix Pointer to a buffer with the command prefix
 * @param suffix Pointer to a buffer with the command suffix
 * @param timeout Timeout for reading a response
 * @return uint8_t The response length
*/
uint8_t ASIM::getReplyQuoted(ASIMFlashString prefix, ASIMFlashString suffix, uint16_t timeout) {
	flushInput();

	DEBUG_PRINT(F("\t ---> "));
	DEBUG_PRINT(prefix);
	DEBUG_PRINT("\"");
	DEBUG_PRINT(suffix);
	DEBUG_PRINTLN("\"");

	simSerial->print(prefix);
	simSerial->print('"');
	simSerial->print(suffix);
	simSerial->println('"');

	uint8_t l = readAnswer(timeout);

	DEBUG_PRINT(F("\t"));
	DEBUG_PRINT(replybuffer);
	DEBUG_PRINTLN(F(" <--- \n"));

	return l;
}

/**
 * @brief Parse a string in the response fields using a designated separator
 * and copy the value at the specified index in to the supplied buffer.
 *
 * @param toreply Pointer to a buffer with reply with the field being parsed
 * @param v Pointer to a buffer to fill with the the value from the parsed field
 * @param divider The divider character
 * @param index The index of the parsed field to retrieve
 * @return true: success, false: failure
*/
bool ASIM::parseReply(ASIMFlashString toreply, uint16_t *v, char divider, uint8_t index) {
	char *p = prog_char_strstr(replybuffer, (prog_char *)toreply);
	if (p == 0)
		return false;
	p += prog_char_strlen((prog_char *)toreply);
	for (uint8_t i = 0; i < index; i++) {
		// increment dividers
		p = strchr(p, divider);
		if (!p)
		return false;
		p++;
	}
  	*v = atoi(p);

	return true;
}

/**

 * @brief Parse a string in the response fields using a designated separator
 * and copy the string at the specified index in to the supplied char buffer.
 *
 * @param toreply Pointer to a buffer with reply with the field being parsed
 * @param v Pointer to a buffer to fill with the string
 * @param divider The divider character
 * @param index The index of the parsed field to retrieve
 * @return true: success, false: failure
*/
bool ASIM::parseReply(ASIMFlashString toreply, char *v, char divider, uint8_t index) {
	uint8_t i = 0;
	char *p = prog_char_strstr(replybuffer, (prog_char *)toreply);
	if (p == 0)
		return false;
	p += prog_char_strlen((prog_char *)toreply);

	for (i = 0; i < index; i++) {
		// increment dividers
		p = strchr(p, divider);
		if (!p)
		return false;
		p++;
	}

	for (i = 0; i < strlen(p); i++) {
		if (p[i] == divider)
		break;
		v[i] = p[i];
	}

	v[i] = '\0';

	return true;
}

/**
 *
 * @brief Parse a string in the response fields using a designated separator
 * and copy the string (without quotes) at the specified index in to the
 * supplied char buffer.
 *
 * @param toreply Pointer to a buffer with reply with the field being parsed
 * @param v Pointer to a buffer to fill with the string. Make sure to supply a
 * buffer large enough to retrieve the expected value
 * @param maxlen The maximum read length
 * @param divider The divider character
 * @param index The index of the parsed field to retrieve
 * @return true: success, false: failure
*/
bool ASIM::parseReplyQuoted(ASIMFlashString toreply, char *v, int maxlen, char divider, uint8_t index) {
	uint8_t i = 0, j;
	// Verify response starts with toreply.
	char *p = prog_char_strstr(replybuffer, (prog_char *)toreply);
	if (p == 0)
		return false;
	p += prog_char_strlen((prog_char *)toreply);

	// Find location of desired response field.
	for (i = 0; i < index; i++) {
		// increment dividers
		p = strchr(p, divider);
		if (!p)
		return false;
		p++;
	}

	// Copy characters from response field into result string.
	for (i = 0, j = 0; j < maxlen && i < strlen(p); ++i) {
		// Stop if a divier is found.
		if (p[i] == divider)
		break;
		// Skip any quotation marks.
		else if (p[i] == '"')
		continue;
		v[j++] = p[i];
	}

	// Add a null terminator if result string buffer was not filled.
	if (j < maxlen)
		v[j] = '\0';

	return true;
}

/**
 * @brief Send data and parse the reply
 *
 * @param tosend Pointer to the data buffer to send
 * @param toreply Pointer to a buffer with the expected reply string
 * @param v Pointer to a uint16_t buffer to hold the value of the parsed response
 * @param divider The divider character
 * @param index The index of the parsed field to retrieve
 * @return true: success, false: failure
*/
bool ASIM::sendParseReply(ASIMFlashString tosend, ASIMFlashString toreply, uint16_t *v, char divider, uint8_t index) {
  getReply(tosend);

  if (!parseReply(toreply, v, divider, index))
    return false;

  readAnswer(); // remove 'OK'

  return true;
}

/**********************************************************************************************************************************/
/**
 * @brief Get type of GSM modem
 *
 * @return uint8_t The type of modem
*/
uint8_t ASIM::getModemType() {
	DEBUG_PRINTLN(F("================= CHECK MODEM TYPE ================="));
	DEBUG_PRINT(F("\t---> "));
  	DEBUG_PRINTLN("ATI");

	simSerial->println("ATI");
	readAnswer(500, true);
	
	DEBUG_PRINT("\t");
	DEBUG_PRINT(replybuffer);
	DEBUG_PRINTLN(F(" <--- \n"));

	if(prog_char_strstr(replybuffer, (prog_char *)F("SIM808 R14"))) {
		DEBUG_PRINTLN(F("Modem type is SIM808 V2"));
		return SIM808_V2;
	}
	else if(prog_char_strstr(replybuffer, (prog_char *)F("SIM808 R13"))) {
		DEBUG_PRINTLN(F("Modem type is SIM808 V1"));
		return SIM808_V1;
	}
	else if(prog_char_strstr(replybuffer, (prog_char *)F("SIM800"))) {
		DEBUG_PRINTLN(F("Modem type is SIM800x"));
		return SIM800;
	}
	else {
		return UNKNOWN_TYPE;
	}
}

/**
 * @brief Get modem IMEI
 *
*/
uint8_t ASIM::getIMEI() {
	char *endpoint;
	DEBUG_PRINTLN(F("================= CHECK MODEM IMEI ================="));
	DEBUG_PRINT(F("\t---> "));
  	DEBUG_PRINTLN("AT+CGSN");


	simSerial->println("AT+CGSN");
	readAnswer(500, true);
	
	
	endpoint = strstr(replybuffer, "OK");
	if(endpoint) {
		*endpoint = NULL;
	}
	DEBUG_PRINT("\t");
	DEBUG_PRINT(replybuffer);
	DEBUG_PRINTLN(F(" <--- \n"));

	strcpy(_imei, replybuffer);
	DEBUG_PRINT(F("MODEM IEMI is "));
	DEBUG_PRINTLN(_imei);
}

/**
 * @brief Get type of SIM card
 *
 * @return uint8_t The type of sim card
*/
uint8_t ASIM::getSimType() {
	DEBUG_PRINTLN(F("================= CHECK SIM TYPE ================="));
	DEBUG_PRINT(F("\t---> "));
  	DEBUG_PRINTLN("AT+COPS?");

	simSerial->println("AT+COPS?");
	readAnswer(500, true);
	
	DEBUG_PRINT("\t");
	DEBUG_PRINT(replybuffer);
	DEBUG_PRINTLN(F(" <--- \n"));

	if(prog_char_strstr(replybuffer, (prog_char *)F("\"43235\""))) {
		DEBUG_PRINTLN(F("SIM type is MTN IRANCELL"));
		return MTN;
	}
	else if(prog_char_strstr(replybuffer, (prog_char *)F("\"TCI\""))) {
		DEBUG_PRINTLN(F("SIM type is HAMRAH-E AVVAL"));
		return MCI;
	}
	else {
		DEBUG_PRINTLN(F("CAN NOT DETECT SIM TYPE"));
		return UNKNOWN_SIM;
	}
}
/**********************************************************************************************************************************/
/**
 * @brief Check modem status and health
 *
 * @return bool true if modem work correctly, false otherwise
*/
bool ASIM::checkModemStatus() {
	bool check_flag = false;
	uint8_t signal = WEAK_SIGNAL;
	uint8_t sim_status = 0b11111111;

	check_flag = checkregistration();
	sim_status = sim_status & check_flag;

	check_flag = checkPIN();
	sim_status = sim_status & check_flag;

	signal = getSignalQuality();
	check_flag = false;
	if(signal != WEAK_SIGNAL) {
		check_flag = true;
	}
	sim_status = sim_status & check_flag;



	DEBUG_PRINTLN(sim_status, BIN);
	DEBUG_PRINTLN(F("================= CHECK GSM STATUS ================="));
	if(sim_status)
		DEBUG_PRINTLN("MODEM WORKS CORRECTLY");
	else
		DEBUG_PRINTLN("MODEM DOES NOT WORK CORRECTLY");
	return sim_status;
}

/**
 * @brief Check simcard registration status 
 *
 * @return bool true if simcard registerd, false otherwise
*/
bool ASIM::checkregistration() {
	char *endpoint;
	bool cmpr_result = false;

	DEBUG_PRINTLN(F("================= CHECK REG ================="));

	cmpr_result = sendVerifyedCommand(F("AT+CREG?"), F("+CREG: 0,1OK"), 500);

	if(!cmpr_result) {
		DEBUG_PRINTLN(F("SIM NOT REGISTERED"));
		return false;
	}
	DEBUG_PRINTLN(F("SIM REGISTERED"));
	return true;
}

/**
 * @brief Check simcard PIN 
 *
 * @return bool true if simcard has no PIN, false otherwise
*/
bool ASIM::checkPIN() {
	char *endpoint;
	int8_t cmpr_result = 1;
	DEBUG_PRINTLN(F("================= CHECK REG ================="));

	cmpr_result = sendVerifyedCommand(F("AT+CPIN?"), F("+CPIN: REDYOK"), 500);

	cmpr_result = strcmp(replybuffer, "+CPIN: REDY");

	if(!cmpr_result) {
		DEBUG_PRINTLN(F("SIM NOT REGISTERED"));
		return false;
	}
	DEBUG_PRINTLN(F("SIM HAS NO PIN & READY TO USE"));
	return true;
}

/**
 * @brief Get quality of signal
 *
 * @return uint8_t signal status (WEAK, MARGINAL, GOOD or EXCELLENT)
*/
uint8_t ASIM::getSignalQuality() {
	uint16_t sgq;
	DEBUG_PRINTLN(F("================= CHECK SIGNAL QUALITY ================="));
	sendParseReply(F("AT+CSQ"), F("+CSQ: "), &sgq);
	if(sgq > 20) {
		DEBUG_PRINT("SIGNAL STRENGTH : ");
		DEBUG_PRINTLN(sgq);
		return EXCELLENT_SIGANL;
	}
	else if((sgq <= 20) && (sgq > 10)) {
		DEBUG_PRINT("SIGNAL STRENGTH : ");
		DEBUG_PRINTLN(sgq);
		return GOOD_SIGNAL;
	}
	else if((sgq <= 10) && (sgq > 4)) {
		DEBUG_PRINT("SIGNAL STRENGTH : ");
		DEBUG_PRINTLN(sgq);
		return MARGINAL_SIGNAL;
	}

	DEBUG_PRINT("SIGNAL STRENGTH : ");
	DEBUG_PRINTLN(sgq);
	return WEAK_SIGNAL;
}
/**********************************************************************************************************************************/
/**
 * @brief Set baudrate to modem
 *
 * @param buad buadrate
 * @return bool true if set successfully, false otherwise
*/
bool ASIM::setBaud(unsigned long baud) {
	char _cmd[20];
	bool is_set = false;
	sprintf(_cmd, "AT+IPR=%lu", baud);

	DEBUG_PRINTLN(F("================= SET BUADRATE ================="));

	is_set = sendVerifyedCommand(_cmd, ok_reply, 500);
	return is_set;
}

/**
 * @brief Set modem to TEXT mode
 *
 * @param format message format
 * @return bool true if set successfully, false otherwise
*/
bool ASIM::setMessageFormat(uint8_t format) {
	DEBUG_PRINTLN(F("================= SET MESSAGE FORMAT ================="));
	return sendVerifyedCommand(F("AT+CMGF="), format, ok_reply, 500);
}

/**
 * @brief Set character encoding
 *
 * @param chs character set
 * @return bool true if set successfully, false otherwise
*/
bool ASIM::setCharSet(char *chs) {
	DEBUG_PRINTLN(F("================= SET CHARACTER SET ================="));
	return sendVerifyedCommandQuoted(F("AT+CSCS="), (ASIMFlashString)chs, ok_reply, 500);
}

/**
 * @brief Set calling line identification presentation
 *
 * @return bool true if set successfully, false otherwise
*/
bool ASIM::setCLI() {
	DEBUG_PRINTLN(F("================= SET CLIP ================="));
	return sendVerifyedCommand(F("AT+CLIP=1"), ok_reply, 500);
}

/**
 * @brief Set SMS parameters
 *
 * @param fo SMS submition methode
 * @param vp absolute time of the validity period termination
 * @param pid TP protocol identifier
 * @param dcs SMS data coding scheme
 * @return bool true if set successfully, false otherwise
*/
bool ASIM::setSMSParameters(uint8_t fo, uint16_t vp, uint8_t pid, uint8_t dcs) {
	char _cmd[24];

	DEBUG_PRINTLN(F("================= SET CLIP ================="));
	sprintf(_cmd, "AT+CSMP=%d,%d,%d,%d", fo, vp, pid, dcs);
	return sendVerifyedCommand(_cmd, ok_reply, 500);
}
/**********************************************************************************************************************************/
/**
 * @brief Call a phone number
 *
 * @param number The reciever number
 * @return bool true if success, false otherwise
*/
bool ASIM::makeCall(char *number) {
	uint16_t sms_mode;
	char _cmd[35] = "ATD+ ";
	uint8_t last_slot = 0;

	DEBUG_PRINTLN(F("================= MAKING CALL ================="));
	// TODO: CHECK of number[0] if it was 0 changes to +98

	strncpy(_cmd + 5, number, min(30, (int)strlen(number))); 
	last_slot = strlen(_cmd);
	_cmd[last_slot] = ';';
  	_cmd[last_slot + 1] = 0;

	return sendVerifyedCommand(_cmd, ok_reply, 10000);
}

/**
 * @brief End the current call
 *
 * @return bool true if success, false otherwise
*/
bool ASIM::hangUp() { 
	DEBUG_PRINTLN(F("================= HANG UP ================="));
	return sendVerifyedCommand(F("ATH0"), ok_reply); 
}

/**
 * @brief Make missed call
 *
 * @param number The reciever number
 * @param hangup_delay The delay for hangup call
 * @return bool true if success, false otherwise
*/
bool ASIM::makeMissedCall(char *number, uint16_t hangup_delay) {
	bool succeed = false;

	DEBUG_PRINTLN(F("================= MAKING MISSED CALL ================="));
	succeed = makeCall(number);
	if(!succeed) {
		return succeed;
	}
	delay(hangup_delay);
	succeed = hangUp();

	return succeed;
}
/**********************************************************************************************************************************/
/**
 * @brief Delete all sms in inbox
 *
 * @return bool true if set successfully, false otherwise
*/
bool ASIM::clearInbox() {
	DEBUG_PRINTLN(F("================= DELETE ALL SMS ================="));
	return sendVerifyedCommand(F("AT+CMGDA=\"DEL ALL\""), ok_reply, 1000);
	//return sendVerifyedCommandQuoted(F("AT+CMGDA="), F("DEL ALL"), ok_reply, 500);
}

/**
 * @brief Delete an SMS Message
 *
 * @param message_index The message to delete
 * @return bool true if success, false otherwise
*/
bool ASIM::deleteSMS(uint8_t message_index) {
	uint16_t sms_mode;
	sendParseReply(F("AT+CMGF?"), F("+CMGF:"), &sms_mode);
	if(sms_mode != TEXT_MODE) {
		DEBUG_PRINTLN("SMS MODE IS NOT ACCEPTABLE");
		return SIM_FAILED;
	}
	// read an sms
	char sendbuff[12] = "AT+CMGD=000";
	sendbuff[8] = (message_index / 100) + '0';
	message_index %= 100;
	sendbuff[9] = (message_index / 10) + '0';
	message_index %= 10;
	sendbuff[10] = message_index + '0';

	return sendVerifyedCommand(sendbuff, ok_reply, 5000);
}

/**
 * @brief Send a SMS Message
 *
 * @param receiver_number The reciever number
 * @param msg The SMS body
 * @return bool true if send SMS successfully, false otherwise
*/
bool ASIM::sendSMS(char *receiver_number, char *msg) {
	uint16_t sms_mode;
	char sendcmd[30] = "AT+CMGS=\"";

	DEBUG_PRINTLN(F("================= SENDING SMS ================="));

	sendParseReply(F("AT+CMGF?"), F("+CMGF:"), &sms_mode);
	if(sms_mode != TEXT_MODE) {
		DEBUG_PRINTLN("SMS MODE IS NOT ACCEPTABLE");
		return SIM_FAILED;
	}
	

	strncpy(sendcmd + 9, receiver_number, 30 - 9 - 2); 
	// 9 bytes for AT+CMGS=", 2 bytes for close quote + null
	sendcmd[strlen(sendcmd)] = '\"';

	if (!sendVerifyedCommand(sendcmd, F("> "))) {
		DEBUG_PRINTLN("SMS BODY INDICATOR ('>') DOES NOT SHOWN");
		return SIM_FAILED;
	}

	simSerial->print(msg);
	simSerial->write(0x1A);

	DEBUG_PRINT(msg);
	DEBUG_PRINTLN(" ^Z");

	// read the +CMGS reply, wait up to 10 seconds!
	readAnswer(10000);

	if ((!strstr(replybuffer, "+CMGS")) || (!strstr(replybuffer, "OK"))) {
		DEBUG_PRINTLN("SMS DID NOT SEND PROPERLY");
		return SIM_FAILED;
	}

	return SIM_OK;
}

/**
 * @brief Read an SMS message into a provided buffer
 *
 * @param message_index The SMS message index to retrieve
 * @param sender The sender number buffer
 * @param body The SMS body
 * @param sms_len The length of the SMS
 * @param maxlen The maximum read length
 * @return bool true if success, false otherwise
*/
bool ASIM::readSMS(uint8_t message_index, char *sender, char *body, uint16_t *sms_len, uint16_t maxlen) {
	uint16_t sms_mode;
	uint16_t thesmslen = 0;
	char *endpoint, *substr;
	char temp[256];
	char sendcmd[30] = "AT+CMGS=\"";

	DEBUG_PRINTLN(F("================= READING SMS ================="));

	sendParseReply(F("AT+CMGF?"), F("+CMGF:"), &sms_mode);
	if(sms_mode != TEXT_MODE) {
		DEBUG_PRINTLN("SMS MODE IS NOT ACCEPTABLE");
		return SIM_FAILED;
	}

	// show all text mode parameters
	if (!sendVerifyedCommand(F("AT+CSDH=1"), ok_reply)) {
		DEBUG_PRINTLN("CAN NOT SHOW ALL SMS PARAMETERS");
		return SIM_FAILED;
	}

	// parse out the SMS len
	DEBUG_PRINT(F("AT+CMGR="));
	DEBUG_PRINTLN(message_index);

	simSerial->print(F("AT+CMGR="));
	simSerial->println(message_index);
	readAnswer(3000);

	// parse it out...
	DEBUG_PRINTLN(replybuffer);

	if (!parseReply(F("+CMGR:"), &thesmslen, ',', 11)) {
		DEBUG_PRINTLN("THERE IS NO SMS WITH INPUT INDEX");
		*sms_len = 0;
		return SIM_FAILED;
	}

	flushInput();

	uint16_t full_reply_len = min(maxlen, (uint16_t)strlen(replybuffer));

	bool result = parseReplyQuoted(F("+CMGR:"), sender, full_reply_len, ',', 1);
	result = parseReplyQuoted(F("+CMGR:"), temp, full_reply_len, ',', 11);
	endpoint = strstr(temp, "OK");
	if(!endpoint) {
		return SIM_FAILED;
	}
	*endpoint = NULL;

	if((thesmslen >= 1) && (thesmslen < 10))
		substr = temp + 1;
	else if((thesmslen >= 10) && (thesmslen < 100))
		substr = temp + 2;
	else if((thesmslen >= 100) && (thesmslen < 1000))
		substr = temp + 3;
	else if((thesmslen >= 1000) && (thesmslen < 10000))
		substr = temp + 4;
	else
		substr = temp + 0;
	
	strncpy(body, substr, strlen(substr));

	*sms_len = thesmslen;

	return SIM_OK;
}

/**
 * @brief Read an SMS message into a provided buffer with more detials
 *
 * @param message_index The SMS message index to retrieve
 * @param sender The sender number buffer
 * @param body The SMS body
 * @param timestamp The timestamp of the SMS sent
 * @param sms_len The length of the SMS
 * @param maxlen The maximum read length
 * @return bool true if success, false otherwise
*/
bool ASIM::readSMS(uint8_t message_index, char *sender, char *body, char *date, char *tyme, uint16_t *sms_len, uint16_t maxlen) {
	uint16_t sms_mode;
	uint16_t thesmslen = 0;
	char *endpoint, *substr;
	char temp[256];
	bool parse_result = false;
	char sendcmd[30] = "AT+CMGS=\"";

	DEBUG_PRINTLN(F("================= READING SMS ================="));

	sendParseReply(F("AT+CMGF?"), F("+CMGF:"), &sms_mode);
	if(sms_mode != TEXT_MODE) {
		DEBUG_PRINTLN("SMS MODE IS NOT ACCEPTABLE");
		return SIM_FAILED;
	}

	// show all text mode parameters
	if (!sendVerifyedCommand(F("AT+CSDH=1"), ok_reply)) {
		DEBUG_PRINTLN("CAN NOT SHOW ALL SMS PARAMETERS");
		return SIM_FAILED;
	}

	// parse out the SMS len
	DEBUG_PRINT(F("AT+CMGR="));
	DEBUG_PRINTLN(message_index);

	simSerial->print(F("AT+CMGR="));
	simSerial->println(message_index);
	readAnswer(3000);

	// parse it out...
	DEBUG_PRINTLN(replybuffer);

	if (!parseReply(F("+CMGR:"), &thesmslen, ',', 11)) {
		DEBUG_PRINTLN("THERE IS NO SMS WITH INPUT INDEX");
		*sms_len = 0;
		return SIM_FAILED;
	}

	flushInput();

	uint16_t full_reply_len = min(maxlen, (uint16_t)strlen(replybuffer));

	parse_result = parseReplyQuoted(F("+CMGR:"), sender, full_reply_len, ',', 1);
	parse_result = parseReplyQuoted(F("+CMGR:"), temp, full_reply_len, ',', 11);
	parse_result = parseReplyQuoted(F("+CMGR:"), date, full_reply_len, ',', 3);
	parse_result = parseReplyQuoted(F("+CMGR:"), tyme, full_reply_len, ',', 4);
	endpoint = strstr(temp, "OK");
	if(!endpoint) {
		return SIM_FAILED;
	}
	*endpoint = NULL;

	if((thesmslen >= 1) && (thesmslen < 10))
		substr = temp + 1;
	else if((thesmslen >= 10) && (thesmslen < 100))
		substr = temp + 2;
	else if((thesmslen >= 100) && (thesmslen < 1000))
		substr = temp + 3;
	else if((thesmslen >= 1000) && (thesmslen < 10000))
		substr = temp + 4;
	else
		substr = temp + 0;
	
	strncpy(body, substr, strlen(substr));

	*sms_len = thesmslen;

	return SIM_OK;
}

/**
 * @brief Get the number of SMS
 *
 * @return int8_t The SMS count. -1 on error
*/
int8_t ASIM::getNumSMS() {
  	uint16_t numsms;
	uint16_t sms_mode;

	DEBUG_PRINTLN(F("================= READING NUMBER SMS IN INBOX ================="));

	sendParseReply(F("AT+CMGF?"), F("+CMGF:"), &sms_mode);
	if(sms_mode != TEXT_MODE) {
		DEBUG_PRINTLN("SMS MODE IS NOT ACCEPTABLE");
		return -1;
	}

	// ask how many sms are stored
	if (sendParseReply(F("AT+CPMS?"), F("\"SM\","), &numsms))
    	return numsms;
	if (sendParseReply(F("AT+CPMS?"), F("\"GSM\","), &numsms))
		return numsms;
	if (sendParseReply(F("AT+CPMS?"), F("\"SM_P\","), &numsms))
		return numsms;
	return -1;
}
/**********************************************************************************************************************************/
/**
 * @brief Send USSD
 *
 * @param ussd_code The USSD message buffer
 * @param ussd_response The USSD response bufer
 * @param response_len The length actually read
 * @param max_len The maximum read length 
 * @return bool true if success, false otherwise
 */
bool ASIM::sendUSSD(char *ussd_code, char *ussd_response, uint16_t *response_len, uint16_t max_len) {
	char _cmd[35] = "AT+CUSD=1,\"";

	DEBUG_PRINTLN(F("================= SENDING USSD ================="));

	if (!sendVerifyedCommand(F("AT+CUSD=1"), ok_reply, 500)) {
		return SIM_FAILED;
	}

  	// make command
	strncpy(_cmd + 11, ussd_code, 35 - 11 - 2); 
	// 11 bytes (AT+CUSD=1,"), 2 bytes for close quote + null
  	_cmd[strlen(_cmd)] = '\"';

	if (!sendVerifyedCommand(_cmd, ok_reply, 500)) {
		*response_len = 0;
		return SIM_FAILED;
	} 
	else {
		readAnswer(10000); // read the +CUSD reply, wait up to 10 seconds!!!
		DEBUG_PRINT("* "); DEBUG_PRINTLN(replybuffer);
		char *p = prog_char_strstr(replybuffer, PSTR("+CUSD: "));
		if (p == 0) {
			*response_len = 0;
			return SIM_FAILED;
		}
		p += 7; //+CUSD
		// Find " to get start of ussd message.
		p = strchr(p, '\"');
		if (p == 0) {
			*response_len = 0;
			return SIM_FAILED;
		}
		p += 1; //"
		// Find " to get end of ussd message.
		char *strend = strchr(p, '\"');

		uint16_t lentocopy = min(max_len - 1, strend - p);
		strncpy(ussd_response, p, lentocopy + 1);
		ussd_response[lentocopy] = 0;
		*response_len = lentocopy;
	}
  return SIM_OK;
}













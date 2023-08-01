/**********************************************************************************************************************************/
#include "ASIM.h"

char replybuffer[255];
/*************************************************************************************************************/
/**
 * @brief Construct a new Ario_SIM object
 *
 * @param port The serial port of GSM
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
bool ASIM::begin(ASIMStreamType &port, int setup_wait) {
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

	delay(setup_wait);
	DEBUG_PRINTLN(F("================= ESTABLIS COMMUNICATON ================="));
	DEBUG_PRINTLN(F("Try communicate with modem (May take 10 seconds to find cellular network)"));

	// give 7 seconds to reboot
	uint16_t timeout = DEFUALT_INIT_WAIT;

	while (timeout > 0) {
		while (simSerial->available())
		 	simSerial->read();
		if (sendVerifyedCommand(F("AT"), F("ATOK"), 500)) {
			break;
		}
		if (sendVerifyedCommand(F("AT"), ok_reply, 500)) {
			break;
		}
		while (simSerial->available())
			simSerial->read();
		if (sendVerifyedCommand(F("AT"), F("AT"), 500))
		  	break;
		delay(500);
		timeout -= 500;
	}

	if(timeout <= 0) {
		DEBUG_PRINTLN("Timeout: No response to AT... last attempt.");
		sendVerifyedCommand(F("AT"), F("ATOK"), 500);
		delay(1000);
	}

	if(!sendVerifyedCommand(F("AT"), F("ATOK"), 500)) {
		if(!sendVerifyedCommand(F("AT"), ok_reply, 500)) {
			return SIM_FAILED;
		}
	}

	DEBUG_PRINTLN(F("================= INIT MODEM ================="));
	DEBUG_PRINTLN(F("The connection with the modem was successfully established"));
	DEBUG_PRINTLN(F("Initializing....(May take 10 seconds)"));

	// Turn of echo
	sendVerifyedCommand(F("ATE0"), F("ATE0OK"));
	delay(100);

	// Get modem type
	_modem_type = getModemType();

	// Get SIM card type
	_sim_type = getSimType();

	// Check modem status
	// checkModemStatus();

	#ifdef FULL_CONFIG
		// Get modem IMEI
		getIMEI();

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
		setCallerIdNotification();

		// Set SMS parameters
		# ifdef SET_SMS_PARAM
			setSMSParameters(49, 167, 0, 0);
		#endif

		// Delete all sms
		clearInbox();

		// Set simcard language to English
		# ifdef SET_LANG_TO_ENG
			setSIMLanguage(ENGLISH);
		#endif
	#endif
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
int ASIM::available(void) { 
	return simSerial->available(); 
} 

/**
 * @brief Serial write
 *
 * @param x
 * @return size_t
*/
size_t ASIM::write(uint8_t x) {
	return simSerial->write(x);
} 

/**
 * @brief Serial read
 *
 * @return int
*/
int ASIM::read(void) { 
	return simSerial->read(); 
}

/**
 * @brief Serial readBytes
 * 
 * @return size_t
 */
size_t ASIM::readBytes(char * buffer, uint16_t sizeOfBuffer) {
	return simSerial->readBytes(buffer, sizeOfBuffer);
}

/**
 * @brief Serial peek
 *
 * @return int
*/
int ASIM::peek(void) { 
	return simSerial->peek(); 
} 

/**
 * @brief Flush the serial data
 *
*/
void ASIM::flush() { 
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
 * @brief Read a single line or up to 254 bytes without new line
 *
 * @param timeout Reply timeout
 * @param multiline true: read the maximum amount. false: read up to the second
 * newline
 * @return uint8_t the number of bytes read
 */
uint8_t ASIM::readAnswer(uint16_t timeout, bool multiline) {
	uint16_t replyidx = 0, o_index = 255;
	bool wiat_for_ok = false;
	bool end_flag = false;

	while (timeout--) {

		while (simSerial->available()) {
			char c = simSerial->read();
			if ((c == '\r') || (c == '\n')) continue;
			if (c == 0xA) {
				if (replyidx == 0) continue; // the first 0x0A is ignored
	
				if (!multiline) {
				  timeout = 0; // the second 0x0A is the end of the line
				  break;
				}
			}

			replybuffer[replyidx] = c;
			replyidx++;

			if(c == 'O') {
				o_index = replyidx;
				wiat_for_ok = true;
			}
			if((wiat_for_ok) && (o_index != replyidx)) {
				if(c == 'K') {
					end_flag = true;
				}
				else {
					wiat_for_ok = false;
				}
			}

			if(end_flag) {
				timeout = 0;
				break;
			}

			if (replyidx > 253) {
				timeout = 0;
				break;
			}
		}

		if (timeout == 0) break;
		delay(1);
	}
	replybuffer[replyidx] = 0; // null term
	return replyidx;
}

/**
 * @brief Read a single line or up to 254 bytes with new line
 *
 * @param timeout Reply timeout
 * @param multiline true: read the maximum amount. false: read up to the second
 * newline
 * @return uint8_t the number of bytes read
 */
uint8_t ASIM::readAnswerLn(uint16_t timeout, bool multiline) {
	uint16_t replyidx = 0;
	while (timeout--) {
		while (simSerial->available()) {
			char c = simSerial->read();
			replybuffer[replyidx] = c;
			replyidx++;
			if (replyidx > 253) {
				timeout = 0;
				break;
			}
		}
		if (timeout == 0) break;
		delay(1);
	}
	replybuffer[replyidx] = 0; // null term
	return replyidx;
}

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
bool ASIM::parseReplyQuoted(char *buffer, ASIMFlashString toreply, char *v, int maxlen, char divider, uint8_t index) {
	uint8_t i = 0, j;
	// Verify response starts with toreply.
	char *p = prog_char_strstr(buffer, (prog_char *)toreply);
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
int8_t ASIM::getSimType() {
	DEBUG_PRINTLN(F("================= CHECK SIM TYPE ================="));
	DEBUG_PRINT(F("\t---> "));
  	DEBUG_PRINTLN("AT+COPS?");

	simSerial->println("AT+COPS?");
	if(readAnswer(500, true) <= 1) {
		return -1;
	} 
	
	DEBUG_PRINT("\t");
	DEBUG_PRINT(replybuffer);
	DEBUG_PRINTLN(F(" <--- \n"));

	if(prog_char_strstr(replybuffer, (prog_char *)F("\"43235\""))) {
		DEBUG_PRINTLN(F("SIM type is MTN IRANCELL"));
		return IRANCELL;
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

/**
 * @brief Check simcard registration status 
 *
 * @return bool true if simcard registerd, false otherwise
*/
bool ASIM::checkregistration() {
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
int8_t ASIM::getSignalQuality() {
	uint16_t sgq;
	DEBUG_PRINTLN(F("================= CHECK SIGNAL QUALITY ================="));
	if(!sendParseReply(F("AT+CSQ"), F("+CSQ: "), &sgq)) {
		return -1;
	}
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

/**
 * @brief Check communication between MCU and GSM
 * 
 * @return bool true if success, false otherwise
 */
bool ASIM::checkConnection(ASIMFlashString reply) {
	bool retVal = false;
	DEBUG_PRINTLN(F("================= CHECK CONNECTION ================="));
	if(sendVerifyedCommand(F("AT"), reply, 500)) {
		retVal = true;
	}else {
		retVal = false;
	}
	return retVal;	
}

/**
 * @brief Echo off
 * 
 * @return bool true if set successfully, false otherwise
 */
bool ASIM::echoOff() {
	DEBUG_PRINTLN(F("================= TURN OF ECHO ================="));
	return sendVerifyedCommand(F("ATE0"), F("ATE0OK"), 500);	
}

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
 * @brief Set modem fuctionality
 *
 * @param mode fuctionality mode(0:minimum, 1:full, 4:disable)
 * @return bool true if set successfully, false otherwise
*/
bool ASIM::setFunctionality(uint8_t mode) {
	DEBUG_PRINTLN(F("================= SET FUNCTIONALITY ================="));
	return sendVerifyedCommand(F("AT+CFUN="), mode, ok_reply, 500);
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
bool ASIM::setCallerIdNotification() {
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

/**
 * @brief Set simcard language
 *
 * @param lang language (FARSI=27 and ENGLISH=37)
 * @return bool true if set successfully, false otherwise
*/
bool ASIM::setSIMLanguage(uint8_t lang) {
	char result[512];
	uint16_t result_len = 0;

	DEBUG_PRINTLN(F("================= SET SIM LANGUAGE ================="));
	if(_sim_type == MCI) {
		DEBUG_PRINTLN("MCI DOES NOT SUPPORT CHANGE LANGUAGE");
		return SIM_FAILED;
	}

	if((_sim_type != IRANCELL) && (lang == FARSI)) {
		DEBUG_PRINTLN("YOUR SIMCARD DOES NOT SUPPORT FARSI LANGUAGE");
		return SIM_FAILED;
	}

	if(lang == ENGLISH) {
		if(_sim_type == IRANCELL) {
			return(sendUSSD("*555*4*3*2#", result, &result_len, 512));
		}
	}

	if(lang == FARSI) {
		if(_sim_type == IRANCELL) {
			return(sendUSSD("*555*4*3*1#", result, &result_len, 512));
		}	
	}
}

/**
 * @brief Reset the modem by software
 *
 * @return bool true if set successfully, false otherwise
*/
bool ASIM::softReset() {
	DEBUG_PRINTLN(F("================= SOFT RESET MODEM ================="));
	return sendVerifyedCommand(F("AT+CFUN=1,1"), ok_reply);

}

/**
 * @brief Reset the modem by hardware
 *
 * @return bool true if set successfully, false otherwise
*/
bool ASIM::hardReset() {
	DEBUG_PRINTLN(F("================= HARD RESET MODEM ================="));
	if((_modem_type == SIM808_V1) || (_modem_type == SIM808_V2)) {
		if(_rst_pin > 0) {
			digitalWrite(_rst_pin, LOW);
			delay(1000);
			digitalWrite(_rst_pin, HIGH);
		}
		else if(_in_pwr_pin > 0) {
			digitalWrite(_in_pwr_pin, LOW);
			delay(1000);
			digitalWrite(_in_pwr_pin, HIGH);
			if(_pwr_key_pin > 0) {
				delay(300);
				digitalWrite(_pwr_key_pin, HIGH);
				delay(1000);
				digitalWrite(_pwr_key_pin, LOW);
			}
		}
		else {
			DEBUG_PRINTLN(F("HARDWARE RESET DOES NOT SUPPORT ON YOUR DEVICE"));
			return SIM_FAILED;
		}
	}
	else if(_modem_type == SIM800 ){
		if(_in_pwr_pin > 0) {
			digitalWrite(_in_pwr_pin, LOW);
			delay(1000);
			digitalWrite(_in_pwr_pin, HIGH);
			if(_pwr_key_pin > 0) {
				delay(300);
				digitalWrite(_pwr_key_pin, HIGH);
				delay(1000);
				digitalWrite(_pwr_key_pin, LOW);
			}
		}
		else {
			DEBUG_PRINTLN(F("HARDWARE RESET DOES NOT SUPPORT ON YOUR DEVICE"));
			return SIM_FAILED;
		}
	}
	else {
		DEBUG_PRINTLN(F("HARDWARE RESET DOES NOT SUPPORT ON YOUR DEVICE"));
		return SIM_FAILED;
	}
	return SIM_OK;
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

/**
 * @brief Make voice call with AMR file
 *
 * @param number The reciever number
 * @param file_id The AMR file ID
 * @return bool true if success, false otherwise
*/
bool ASIM::makeAMRVoiceCall(char *number, uint16_t file_id) {
	char amr_cmd[40];
	DEBUG_PRINTLN(F("================= MAKING VOICE CALL WITH AMR FILE ================="));
	
	sprintf(amr_cmd, "AT+CREC=4,\"C:\\User\\%d.amr\",0,90", file_id);
	// AT+CREC=4,"C:\User\file_id.amr",0,90
	makeCall(number);
	// After user pick the phone on
	// TODO: Check if user picked the phone on
	return sendVerifyedCommand(amr_cmd, ok_reply);
}

/**
 * @brief Get the number of the incoming call
 *
 * @param phone_number Pointer to a buffer to hold the incoming caller's phone number
 * @return bool true if success, false otherwise
*/
bool ASIM::incomeCallNumber(char *phone_number) {
	char *substr, *endpoint;
	// RING+CLIP: "<incoming phone number>",145,"",0,"",0
	// or
	// +CLIP: "<incoming phone number>",145,"",0,"",0
	readAnswer(); // reads incoming phone number line
	if(strstr(replybuffer, "RING")) {
		if(strstr(replybuffer, "+CLIP")) {
			substr = replybuffer + 12;
		}
		else {
			DEBUG_PRINTLN("CALLER ID NOTIFICATION IS DISABELD");
			return SIM_FAILED;
		}
	}
	else {
		if(strstr(replybuffer, "+CLIP")) {
			substr = replybuffer + 8;
		}
		else {
			DEBUG_PRINTLN("NO INCOMING CALL DETECTED");
			return SIM_FAILED;			
		}
	}

	endpoint = strstr(substr, "\"");
	if(!endpoint) {
		DEBUG_PRINTLN("CAN NOT PARSE INCOME PHONE NUMBER");
		return SIM_FAILED;
	}
	*endpoint = NULL;

	DEBUG_PRINT(F("Phone Number: "));
	DEBUG_PRINTLN(substr);
	strcpy(phone_number, substr);

	_incoming_call = false;
	return SIM_OK;
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
bool ASIM::sendSMS(char *receiver_number, char *msg, bool hex) {
	uint16_t sms_mode;
	uint16_t wait_to_send = 10000;
	char sendcmd[30] = "AT+CMGS=\"";

	DEBUG_PRINTLN(F("================= SENDING SMS ================="));
	if(hex) {
		setCharSet(HEX_CHARSET);
		setSMSParameters(49, 167, 0, 8);
		wait_to_send = 15000;
	}
	else {
		setCharSet(DEFUALT_CHARSET);
		setSMSParameters(49, 167, 0, 0);
	}

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
		delay(1000);
		setCharSet(DEFUALT_CHARSET);
		setSMSParameters(49, 167, 0, 0);
		return SIM_FAILED;
	}

	simSerial->print(msg);
	simSerial->write(0x1A);

	DEBUG_PRINT(msg);
	DEBUG_PRINTLN(" ^Z");

	// read the +CMGS reply, wait up to 10 seconds!
	readAnswer(wait_to_send);
	DEBUG_PRINTLN(replybuffer);

	if ((!strstr(replybuffer, "+CMGS")) || (!strstr(replybuffer, "OK"))) {
		DEBUG_PRINTLN("SMS DID NOT SEND PROPERLY");
		delay(1000);
		setCharSet(DEFUALT_CHARSET);
		setSMSParameters(49, 167, 0, 0);
		return SIM_FAILED;
	}

	setCharSet(DEFUALT_CHARSET);
	setSMSParameters(49, 167, 0, 0);

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
	char temp[maxlen];
	char datetime[25];
	char div[256];
	char sendcmd[30] = "AT+CMGS=\"";

	DEBUG_PRINTLN(F("================= READING SMS ================="));
	setCharSet(DEFUALT_CHARSET);
	setSMSParameters(49, 167, 0, 0);

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
	readAnswerLn(1000);
	flushInput();
	// parse it out...
	uint16_t full_reply_len = min(maxlen, (uint16_t)strlen(replybuffer));
	endpoint = strstr(replybuffer, "OK");
	if(!endpoint) {
		DEBUG_PRINTLN("THERE IS NO SMS WITH REQUESTED INDEX");
		*sms_len = 0;
		return SIM_FAILED;
	}
	DEBUG_PRINTLN(replybuffer);
	bool result = parseReplyQuoted(replybuffer, F("+CMGR:"), sender, full_reply_len, ',', 1);
	result = parseReplyQuoted(replybuffer, F("+CMGR:"), body, full_reply_len, 0x0A, 1);
	endpoint = strstr(body, "\r");
	if(endpoint) {
		*endpoint = NULL;
	}

	*sms_len = strlen(body);

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
bool ASIM::readSMS(uint8_t message_index, char *sender, char *body, char *date, char *tyme, char *type, uint16_t *sms_len, uint16_t maxlen) {
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
	readAnswerLn(1000);
	flushInput();
	// parse it out...
	DEBUG_PRINTLN(replybuffer);
	uint16_t full_reply_len = min(maxlen, (uint16_t)strlen(replybuffer));
	endpoint = strstr(replybuffer, "OK");
	if(!endpoint) {
		DEBUG_PRINTLN("THERE IS NO SMS WITH REQUESTED INDEX");
		*sms_len = 0;
		return SIM_FAILED;
	}

	parse_result = parseReplyQuoted(replybuffer, F("+CMGR:"), type, full_reply_len, '\"', 1);
	parse_result = parseReplyQuoted(replybuffer, F("+CMGR:"), sender, full_reply_len, ',', 1);
	parse_result = parseReplyQuoted(replybuffer, F("+CMGR:"), date, full_reply_len, ',', 3);
	parse_result = parseReplyQuoted(replybuffer, F("+CMGR:"), tyme, full_reply_len, ',', 4);
	parse_result = parseReplyQuoted(replybuffer, F("+CMGR:"), body, full_reply_len, 0x0A, 1);
	endpoint = strstr(body, "\r");
	if(endpoint) {
		*endpoint = NULL;
	}

	*sms_len = strlen(body);

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
/**********************************************************************************************************************************/
/**
 * @brief Enable GPRS
 *
 * @return bool true if success, false otherwise
*/
bool ASIM::enableGPRS() {
	char network_apn[15];
	char *endpoint;

	DEBUG_PRINTLN(F("================= ENABLING GPRS ================="));
	// Check if sim registerd in GPRS network
	if(!sendVerifyedCommand(F("AT+CGATT?"), F("+CGATT: 1OK"), 1000)) {
		if(!sendVerifyedCommand(F("AT+CGATT=1"), ok_reply, 7000)) {
			DEBUG_PRINTLN("SIMCARD DOES NOT REGISTERD ON GPRS NETWORK");
			_gprs_on = false;
			_tcp_running = false;
			return SIM_FAILED;
		}
	}
	// close all old connections
	if (!sendVerifyedCommand(F("AT+CIPSHUT"), F("SHUT OK"), 4000)) {
		DEBUG_PRINTLN("CAN NOT SHUTDOWN PREVIOUS CONNECTION!");
		_gprs_on = false;
		_tcp_running = false;
		return SIM_FAILED;
	}
	if (!sendVerifyedCommand(F("AT+CIPMUX=0"), ok_reply)) {
		DEBUG_PRINTLN("CAN NOT SET UP IP CONNECTION");
		return SIM_FAILED;
	}

	switch (_sim_type)
	{
		case IRANCELL:
			strcpy(network_apn, "mtnirancell");
			break;
		case MCI:
			strcpy(network_apn, "mcinet");
			break;
		case RITEL:
			strcpy(network_apn, "RighTel");
			break;		
		default:
			DEBUG_PRINTLN("UNKNOWN APN");
			return SIM_FAILED;
			break;
	}
    if (!sendVerifyedCommand(F("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\""), ok_reply, 2000)) {
		DEBUG_PRINTLN("CAN NOT ASIGN GPRS BEARER PROFILE");
		_gprs_on = false;
		return false;
	}

	if(!sendVerifyedCommandQuoted(F("AT+SAPBR=3,1,\"APN\","), F(network_apn), ok_reply, 1000)) {
		DEBUG_PRINTLN("CAN NOT BEARER PROFILE ACCESS POIN NAME");
		_gprs_on = false;
		_tcp_running = false;
		return SIM_FAILED;
	}

	if(!sendVerifyedCommandQuoted(F("AT+CSTT="), F(network_apn), ok_reply, 1000)) {
		DEBUG_PRINTLN("APN DOES NOT RECOGNIZED");
		_gprs_on = false;
		_tcp_running = false;
		return SIM_FAILED;
	}

	// Turn on GPRS
	sendVerifyedCommand(F("AT+SAPBR=1,1"), ok_reply, 20000);
	if(!sendVerifyedCommand(F("AT+CIICR"), ok_reply, 20000)) {
		DEBUG_PRINTLN("CAN NOT CONNECT TO APN");
		_gprs_on = false;
		_tcp_running = false;
		return SIM_FAILED;
	}

	sendVerifyedCommand(F("AT+SAPBR=2,1"), ok_reply, 2000);
	parseReplyQuoted(replybuffer, F("+SAPBR: "), _modem_ip, 15, ',', 2);
	endpoint = strstr(_modem_ip, "OK");
	if(endpoint) {
		*endpoint = NULL;
	}

	if(!_modem_ip) {
		DEBUG_PRINTLN("CAN NOT ASIGN IP");
		_gprs_on = false;
		_tcp_running = false;
	}

	DEBUG_PRINT("MODEM IP IS ");
	DEBUG_PRINTLN(_modem_ip);
	DEBUG_PRINTLN();
	
	_gprs_on = true;
	return SIM_OK;
}

/**
 * @brief Disable GPRS
 *
 * @return bool true if success, false otherwise
*/
bool ASIM::disableGPRS() {
	DEBUG_PRINTLN(F("================= DISABLING GPRS ================="));
	// close all connections
	if (!sendVerifyedCommand(F("AT+CIPSHUT"), F("SHUT OK"), 4000)) {
		DEBUG_PRINTLN("CAN NOT SHUTDOWN PREVIOUS CONNECTION!");
		return SIM_FAILED;
	}

	// close GPRS context
	if(!sendVerifyedCommand(F("AT+SAPBR=0,1"), ok_reply, 3000)) {
		DEBUG_PRINTLN("CAN NOT CLOSE GPRS CONTEXT");
		return SIM_FAILED;
	}

	// Remove modem from network
	if(!sendVerifyedCommand(F("AT+CGATT=0"), ok_reply, 3000)) {
		DEBUG_PRINTLN("CAN NOT REMOVE MODEM FROM NETWORK");
		return SIM_FAILED;
	}
	
	_gprs_on = false;
	_tcp_running = false;
	return SIM_OK;
}

/**
 * @brief Get GSM location from GPRS
 *
 * @param lat Pointer to a buffer to hold the latitude
 * @param lon Pointer to a buffer to hold the longitude
 * @return bool true if success, false otherwise
*/
bool ASIM::getGPRSLocation(uint16_t *error, float *lat, float *lon) {
	uint16_t returncode;
	char error_code[120];
  	char gps_buffer[120];
	DEBUG_PRINTLN(F("================= GET GPRS LOCATION ================="));
	getReply(F("AT+CIPGSMLOC=1,1"), (uint16_t)10000);
	if (!parseReply(F("+CIPGSMLOC: "), error)) {
		DEBUG_PRINTLN("CAN NOT GET LOCATION DUE TO ERROR");
		return SIM_FAILED;
	}
	char *p = replybuffer + 14;
  	uint16_t lentocopy = (int)strlen(p);
  	strncpy(gps_buffer, p, lentocopy + 1);
  	readAnswer(); // eat OK

	// +CIPGSMLOC: 0,-74.007729,40.730160,2015/10/15,19:24:55
	// tokenize the gps buffer to locate the lat & long
	char *longp = strtok(gps_buffer, ",");
	if (!longp) {
		DEBUG_PRINTLN("CAN NOT CALCULATE LONGITUDE");
		return SIM_FAILED;
	}

	char *latp = strtok(NULL, ",");
	if (!latp) {
		DEBUG_PRINTLN("CAN NOT CALCULATE LATITUDE");
		return SIM_FAILED;
	}

	*lat = atof(latp);
	*lon = atof(longp);

    return SIM_OK;
}
/**********************************************************************************************************************************/
/**
 * @brief Initialize HTTP
 *
 * @return bool true if success, false otherwise
*/
bool ASIM::initHttp() {
	DEBUG_PRINTLN(F("================= INIT HTTP ================="));
	return sendVerifyedCommand(F("AT+HTTPINIT"), ok_reply);
}

/**
 * @brief Terminate HTTP
 *
 * @return bool true if success, false otherwise
*/
bool ASIM::termHttp() {
	DEBUG_PRINTLN(F("================= TERMINATE HTTP ================="));
  	return sendVerifyedCommand(F("AT+HTTPTERM"), ok_reply);
}

/**
 * @brief Send HTTP parameter
 *
 * @param parameter Pointer to a buffer with the parameter to send
 * @param value Pointer to a buffer with the parameter value
 * @return bool true if success, false otherwise
*/
bool ASIM::setHttpParameter(ASIMFlashString parameter, const char *value) {
	char param_cmd[50];
	DEBUG_PRINTLN(F("================= SET HTTP PARAMETER ================="));
	flushInput();

	sprintf(param_cmd, "AT+HTTPPARA=\"%s\",\"%s\"", parameter, value);
	return sendVerifyedCommand(param_cmd, ok_reply);

	// DEBUG_PRINT(F("\t---> "));
	// DEBUG_PRINT(F("AT+HTTPPARA=\""));
	// DEBUG_PRINT(parameter);
	// DEBUG_PRINTLN('"');

	// simSerial->print(F("AT+HTTPPARA=\""));
	// simSerial->print(parameter);
	// simSerial->print(F("\",\""));
	// simSerial->print(value);
	// simSerial->println('"');

}

/**
 * @brief Send HTTP parameter
 *
 * @param parameter Pointer to a buffer with the parameter to send
 * @param value Pointer to a buffer with the parameter value
 * @return bool true if success, false otherwise
*/
bool ASIM::setHttpParameter(ASIMFlashString parameter, ASIMFlashString value) {
	char param_cmd[50];
	DEBUG_PRINTLN(F("================= SET HTTP PARAMETER ================="));
	flushInput();

	sprintf(param_cmd, "AT+HTTPPARA=\"%s\",\"%s\"", parameter, value);
	return sendVerifyedCommand(param_cmd, ok_reply);
}

/**
 * @brief Send HTTP parameter
 *
 * @param parameter Pointer to a buffer with the parameter to send
 * @param value The parameter value
 * @return bool true if success, false otherwise
*/
bool ASIM::setHttpParameter(ASIMFlashString parameter, int32_t value) {
	char param_cmd[50];
	DEBUG_PRINTLN(F("================= SET HTTP PARAMETER ================="));
	sprintf(param_cmd, "AT+HTTPPARA=\"%s\",\"%u\"", parameter, value);

	flushInput();

	return sendVerifyedCommand(param_cmd, ok_reply);
}

/**
 * @brief Begin sending data via HTTP
 *
 * @param size The amount of data to be sent in bytes
 * @param maxTime The maximum amount of time in which to send the data, in milliseconds
 * @return bool true if success, false otherwise
*/
bool ASIM::setHttpDataParameter(uint32_t size, uint32_t max_wait) {
	char param_data_cmd[50];
	DEBUG_PRINTLN(F("================= SET HTTP DATA PARAMETER ================="));
	sprintf(param_data_cmd, "AT+HTTPDATA=%u,%u", size, max_wait);
	
	flushInput();

	return sendVerifyedCommand(param_data_cmd, F("DOWNLOAD"));

	// DEBUG_PRINT(F("\t---> "));
	// DEBUG_PRINT(F("AT+HTTPDATA="));
	// DEBUG_PRINT(size);
	// DEBUG_PRINT(',');
	// DEBUG_PRINTLN(max_wait);

	// simSerial->print(F("AT+HTTPDATA="));
	// simSerial->print(size);
	// simSerial->print(",");
	// simSerial->println(max_wait);

	// return expectReply(F("DOWNLOAD"));
}

/**
 * @brief Make an HTTP Request
 *
 * @param method The request method:
 * * 0: GET
 * * 1: POST
 * * 2: HEAD
 * @param status Pointer to a uint16_t to hold the request status as an RFC2616
 * @param datalen Pointer to the  a `uint16_t` to hold the length of the data read
 * @param timeout Timeout for waiting for response
 * @return bool true if success, false otherwise
*/
bool ASIM::setHttpAction(uint8_t method, uint16_t *status, uint16_t *data_len, int32_t timeout) {
	DEBUG_PRINTLN(F("================= MAKE HTTP ACTION ================="));
	if (!sendVerifyedCommand(F("AT+HTTPACTION="), method, ok_reply)) {
		return SIM_FAILED;
	}

	readAnswer(timeout);

	if (!parseReply(F("+HTTPACTION:"), status, ',', 1)) {
		SIM_FAILED;
	}
  	if (!parseReply(F("+HTTPACTION:"), data_len, ',', 2)) {
		SIM_FAILED;
	}

	return SIM_OK;
}

/**
 * @brief Read all available HTTP data
 *
 * @param data_len Pointer to the  a `uint16_t` to hold the length of the data read
 * @return bool true if success, false otherwise
*/
bool ASIM::readHttpResponse(uint16_t *data_len) {
	DEBUG_PRINTLN(F("================= READ HTTP RESPONSE ================="));
	getReply(F("AT+HTTPREAD"));
	if (!parseReply(F("+HTTPREAD:"), data_len, ',', 0)) {
		SIM_FAILED;
	}

	return SIM_OK;
}

/**
 * @brief Start an HTTP POST request
 *
 * @param url string of the target URL to POST
 * @param auth_token string of the authorization token
 * @param data string of data that wanted to POST
 * @return bool true if success, false otherwise
*/
bool ASIM::postHttpRequest(String url, String auth_token, String data, uint16_t server_timeout, char *server_response) {
	int result = SIM_FAILED;
	char *endpoint;
	String temp_cmd = "";
	char temp_buffer[256];
	DEBUG_PRINTLN(F("================= HTTP POST REQUEST ================="));
	// Check if GPRS is off and try to turn it on
	if(!_gprs_on) {
		sendVerifyedCommand(F("AT+SAPBR=2,1"), ok_reply, 2000);
		parseReplyQuoted(replybuffer, F("+SAPBR: "), _modem_ip, 15, ',', 2);
		endpoint = strstr(_modem_ip, "OK");
		if(endpoint) {
			*endpoint = NULL;
		}
		DEBUG_PRINT("MODEM IP IS ");
		DEBUG_PRINTLN(_modem_ip);
		if(strcmp(_modem_ip, "0.0.0.0") == 0) {
			DEBUG_PRINTLN(F("GPRS IS OFF, LET's TURN IT ON"));
			if(!enableGPRS()) {
				DEBUG_PRINTLN(F("CAN NOT TURN ON GPRS !!!"));
				return SIM_FAILED;
			}
		}
	}
	// Handle any pending
	termHttp();
	delay(2);

	// Init HTTP
	if(!initHttp()) {
		DEBUG_PRINTLN(F("CAN NOT INIT HTTP SECTION !!!"));
		termHttp();
		disableGPRS();
		return SIM_FAILED;
	}
	// Specify CID
	if (!setHttpParameter(F("CID"), 1)) {
		DEBUG_PRINTLN(F("CAN NOT SPECIFY CID = 1"));
		termHttp();
		disableGPRS();
		return SIM_FAILED;
	}

	// Specify USERDATA
	DEBUG_PRINTLN(F("================= SET HTTP PARAMETER ================="));
	// temp_cmd = "AT+HTTPPARA=\"USERDATA\",\"Authorization:Token ";
	temp_cmd = "AT+HTTPPARA=\"USERDATA\",\"Authorization:Bearer ";
	temp_cmd += auth_token;
	temp_cmd += "\"";
	temp_cmd.toCharArray(temp_buffer, temp_cmd.length()+1);
	getReply(temp_buffer, 2000);
	if(strcmp(replybuffer, "OK") != 0) {
		DEBUG_PRINTLN(F("CAN NOT SPECIFY TOKEN"));
		termHttp();
		disableGPRS();
		return SIM_FAILED;
	}
	memset(temp_buffer, 0, temp_cmd.length()+1);
	temp_cmd = "";

	// Specified URL
	DEBUG_PRINTLN(F("================= SET HTTP PARAMETER ================="));
	temp_cmd = "AT+HTTPPARA=\"URL\",\"";
	temp_cmd += url;
	temp_cmd += "\"";
	temp_cmd.toCharArray(temp_buffer, temp_cmd.length()+1);
	getReply(temp_buffer, 2000);
	if(strcmp(replybuffer, "OK") != 0) {
		DEBUG_PRINTLN(F("CAN NOT SPECIFY URL"));
		termHttp();
		disableGPRS();
		return SIM_FAILED;
	}
	memset(temp_buffer, 0, temp_cmd.length()+1);
	temp_cmd = "";

	// Specified CONTENT type
	if (!setHttpParameter(F("CONTENT"), "application/json")) {
		DEBUG_PRINTLN(F("CAN NOT SET CONTENT TYPE"));
		termHttp();
		disableGPRS();
		return SIM_FAILED;
	}

	// Set data len and max wait
	if(!setHttpDataParameter(200, 2000)) {
		DEBUG_PRINTLN(F("CAN NOT SET DATA PARAMETERS"));
		termHttp();
		disableGPRS();
		return SIM_FAILED;
	}
	delay(10);

	// Send data
	DEBUG_PRINTLN(F("================= SEND HTTP DATA ================="));
	data.toCharArray(temp_buffer, data.length()+1);
	if(!sendVerifyedCommand(temp_buffer, ok_reply, 2000)) {
		DEBUG_PRINTLN(F("CAN NOT SEND DATA IN HTTP REQUEST"));
		termHttp();
		disableGPRS();
		return SIM_FAILED;
	}

	// Set POST action
	DEBUG_PRINTLN(F("================= SEND HTTP ACTION ================="));
	if(!sendVerifyedCommand(F("AT+HTTPACTION="), 1, ok_reply)) {
		DEBUG_PRINTLN(F("CAN NOT SEND POST REQUEST"));
		termHttp();
		disableGPRS();
		return SIM_FAILED;
	}
	delay(12000);

	// Read server response
	getReply(F("AT+HTTPREAD"), server_timeout);
	delay(200);
	// readAnswer(server_timeout, 1);
	DEBUG_PRINT("server response = ");
	DEBUG_PRINTLN(replybuffer);
	// Extract main response
	parseReplyQuoted(replybuffer, F("+HTTPREAD"), server_response, sizeof replybuffer, ': ', 1);


	return SIM_OK;
}
/**********************************************************************************************************************************/
/**
 * @brief Get current status of TCP connection
 *
 * @return TCP status
*/
uint8_t ASIM::getTCPStatus() {
	char con_status[256];
	char *substr;

	DEBUG_PRINTLN(F("================= READ TCP STATUS ================="));
	DEBUG_PRINTLN("\t---> AT+CIPSTATUS");
	simSerial->println(F("AT+CIPSTATUS"));
	readAnswer(1000);

	DEBUG_PRINT("\t");
	DEBUG_PRINT(replybuffer);
	DEBUG_PRINTLN(" <---");
	substr = replybuffer + 9;
	strcpy(con_status, substr);

	if(strcmp(con_status, "IP INITIAL") == 0) {
		_tcp_running = false;
		return IP_INITIAL;
	}
	if(strcmp(con_status, "IP START") == 0) {
		return IP_START;
	}
	if(strcmp(con_status, "IP CONFIG") == 0) {
		return IP_CONFIG;
	}
	if(strcmp(con_status, "IP GPRSACT") == 0) {
		return IP_GPRSACT;
	}
	if(strcmp(con_status, "IP STATUS") == 0) {
		return IP_STATUS;
	}
	if(strcmp(con_status, "TCP CONNECTING") == 0) {
		return TCP_CONNECTING;
	}
	if(strcmp(con_status, "CONNECT OK") == 0) {
		_tcp_running = true;
		return TCP_CONNECTED;
	}
	if(strcmp(con_status, "TCP CLOSING") == 0) {
		return TCP_CLOSING;
	}
	if(strcmp(con_status, "TCP CLOSED") == 0) {
		return TCP_CLOSED;
	}
	if(strcmp(con_status, "PDP DEACT") == 0) {
		return PDP_DEACTIVATED;
	}

	return IP_INITIAL;
}

/**
 * @brief Establish a TCP connection
 *
 * @return bool true if success, false otherwise
*/
bool ASIM::establishTCP() {
	DEBUG_PRINTLN(F("================= ESTABLISH TCP CONNECTION ================="));

	if(!_gprs_on) {
		_gprs_on = enableGPRS();
		if(!_gprs_on) { 
			DEBUG_PRINTLN("CAN NOT TURN ON GPRS");
			_gprs_on = false;
			_tcp_running = false;
			return SIM_FAILED;
		}
	}

	DEBUG_PRINTLN("\t---> AT+CIFSR");
	simSerial->println(F("AT+CIFSR"));
	readAnswer(3000);
	DEBUG_PRINT("\t");
	DEBUG_PRINT(replybuffer);
	DEBUG_PRINTLN(" <---");
	strcpy(_modem_ip, replybuffer);
	if(!_modem_ip) {
		DEBUG_PRINTLN("CAN NOT ASSIGN IP ADDRESS");
		_gprs_on = false;
		_tcp_running = false;
		return SIM_FAILED;
	}
	DEBUG_PRINT("MODEM IP IS ");
	DEBUG_PRINTLN(_modem_ip);
	DEBUG_PRINTLN();

	_tcp_running = true;

	return SIM_OK;
}

/**
 * @brief Start a TCP connection
 *
 * @param server Pointer to a buffer with the server to connect to
 * @param port Pointer to a buffer witht the port to connect to
 * @return bool true if success, false otherwise
*/
bool ASIM::startTCP(char *server, uint16_t port) {
	bool con_status = false;
	DEBUG_PRINTLN(F("================= STARTING TCP ================="));
	if((!_gprs_on) || (!_tcp_running)) {
		con_status = establishTCP();
		if(!con_status) {
			DEBUG_PRINTLN("CAN NOT ESTABLISH A TCP CONNECTION");
			_gprs_on = false;
			_tcp_running = false;
			return SIM_FAILED;
		}
	}
	DEBUG_PRINT(F("\t --->"));
	DEBUG_PRINT(F("AT+CIPSTART=\"TCP\",\""));
	DEBUG_PRINT(server);
	DEBUG_PRINT(F("\",\""));
	DEBUG_PRINT(port);
	DEBUG_PRINTLN(F("\""));

	simSerial->print(F("AT+CIPSTART=\"TCP\",\""));
	simSerial->print(server);
	simSerial->print(F("\",\""));
	simSerial->print(port);
	simSerial->println(F("\""));

	readAnswer(500);
	DEBUG_PRINT("\t");
	DEBUG_PRINT(replybuffer);
	DEBUG_PRINTLN(" <---");
	if(strcmp(replybuffer, "OK") != 0) {
		DEBUG_PRINTLN("CAN NOT SEND REQUEST TO TCP SERVER");
		_tcp_running = false;
		return SIM_FAILED;
	}

	readAnswer(4500);
	DEBUG_PRINT("\t");
	DEBUG_PRINT(replybuffer);
	DEBUG_PRINTLN(" <---");
	if(strcmp(replybuffer, "CONNECT OK") != 0) {
		DEBUG_PRINTLN("CAN NOT CONNECT TO TCP SERVER");
		_tcp_running = false;
		return SIM_FAILED;
	}

	return SIM_OK;
}

/**
 * @brief Close the TCP connection
 *
 * @return bool true if success, false otherwise
*/
bool ASIM::closeTCP() {
	DEBUG_PRINTLN(F("================= CLOSING TCP ================="));
	return sendVerifyedCommand(F("AT+CIPCLOSE"), F("CLOSE OK"));
}

/**
 * @brief Send data via TCP
 *
 * @param data Pointer to a buffer with the data to send
 * @param response Pointer to buffer to store server response
 * @return bool true if success, false otherwise
*/
bool ASIM::sendTCPData(char *data, char *response) {
	char *substr;
	bool send_result = false;

	DEBUG_PRINTLN(F("================= SENDING TCP MESSAGE ================="));
	if(!_tcp_running) {
		DEBUG_PRINTLN("CAN NOT DETECT TCP CONNECTION");
		return SIM_FAILED;
	}

	send_result = sendVerifyedCommand(F("AT+CIPSEND"), F("> "));
	if(!send_result) {
		DEBUG_PRINTLN("CAN NOT INIT TCP MESSAGE");
		_tcp_running = false;
		return SIM_FAILED;
	}

	simSerial->print(data);
	simSerial->write(0x1A);

	DEBUG_PRINT(data);
	DEBUG_PRINTLN(" ^Z");
	
	readAnswer(7000);
	DEBUG_PRINTLN(replybuffer);

	if ((!strstr(replybuffer, "SEND OK"))) {
		DEBUG_PRINTLN("FAILED TO SEND TCP DATA");
		return SIM_FAILED;
	}

	substr = replybuffer + 7;
	strcpy(response, substr);

	return SIM_OK;
}
/**********************************************************************************************************************************/
/**
 * @brief Enable the Real Time Clock
 *
 * @param mode  1: Enable 0: Disable
 * @return bool true if success, false otherwise
*/
bool ASIM::initRTC(uint8_t mode) {
	DEBUG_PRINTLN(F("================= INIT LOCAL RTC ================="));
	if (!sendVerifyedCommand(F("AT+CLTS="), mode, ok_reply, 500)) {
		return SIM_FAILED;
	}
	return sendVerifyedCommand(F("AT&W"), ok_reply, 500);
}

/**
 * @brief Set the Real Time Clock
 *
 *
 * @param year year data
 * @param month month data
 * @param day day data
 * @param hr hour data
 * @param min minute data
 * @param sec seconde data
 * @return bool true if success, false otherwise
*/
bool ASIM::setRTC(uint8_t year, uint8_t month, uint8_t day, uint8_t hr, uint8_t min, uint8_t sec, int8_t zz) {
	char rtc_cmd[50];
	DEBUG_PRINTLN(F("================= SET RTC ================="));
	sprintf(rtc_cmd,"AT+CCLK=\"%02d/%02d/%02d,%02d:%02d:%02d+%d\"", year, month, day, hr, min, sec, zz);

	return sendVerifyedCommand(rtc_cmd, ok_reply);
}

/**
 * @brief Read the Real Time Clock
 *
 * @param year Pointer to a uint8_t to be set with year data
 * @param month Pointer to a uint8_t to be set with month data **NOT WORKING**
 * @param day Pointer to a uint8_t to be set with day data **NOT WORKING**
 * @param hr Pointer to a uint8_t to be set with hour data **NOT WORKING**
 * @param min Pointer to a uint8_t to be set with minute data **NOT WORKING**
 * @param sec Pointer to a uint8_t to be set with year data **NOT WORKING**
 * @return bool true if success, false otherwise
*/
bool ASIM::readRTC(uint8_t *year, uint8_t *month, uint8_t *day, uint8_t *hr, uint8_t *min, uint8_t *sec) {
	DEBUG_PRINTLN(F("================= GET RTC ================="));
	getReply(F("AT+CCLK?"), (uint16_t) 100); //Get RTC timeout 100 msec
	if (strncmp(replybuffer, "+CCLK: ", 7) != 0)
		return false;

	char *p = replybuffer+8;	// skip +CCLK: "
	
	// Parse date
	int reply = atoi(p); 
	*year = (uint8_t) reply; 
	p+=3;	// skip 3 char

	reply = atoi(p);
	*month = (uint8_t) reply;
	p+=3;

	reply = atoi(p);
	*day = (uint8_t) reply;
	p+=3;

	reply = atoi(p);
	*hr = (uint8_t) reply;
	p+=3;

	reply = atoi(p);
	*min = (uint8_t) reply;
	p+=3;

	reply = atoi(p);
	*sec = (uint8_t) reply;

	return SIM_OK;
}

/**
 * @brief Syncronise time with NTP server
 *
 * @param ntpserver The NTP server buffer
 * @return bool true if success, false otherwise
*/
bool ASIM::syncNTPTime(uint16_t *error_code, char *ntp_server, uint8_t region) {
	char ntp_cmd[40];
	DEBUG_PRINTLN(F("================= SYNC TIME WITH NTP SERVER ================="));
	if(!sendVerifyedCommand(F("AT+CNTPCID=1"), ok_reply)) {
		DEBUG_PRINTLN(F("CAN NOT ENABLE NTP SERVER"));
		return SIM_FAILED;
	}
	if(strlen(ntp_server) > 1) {
		sprintf(ntp_cmd, "AT+CNTP=\"%s\",%d", ntp_server, region);
	}
	else {
		sprintf(ntp_cmd, "AT+CNTP=\"pool.ntp.org\",%d", region);
	}

	if(!sendVerifyedCommand(ntp_cmd, ok_reply, 3000)) {
		DEBUG_PRINTLN(F("CAN NOT CONNECT TO NTP SERVER"));
		return SIM_FAILED;
	}

	sendVerifyedCommand(F("AT+CNTP"), ok_reply, 5000);
	if (!parseReply(F("+CNTP:"), error_code)) {
		DEBUG_PRINTLN(F("SERVER DID NOT RESPOND"));
		return SIM_FAILED;	
	}

	return SIM_OK;
}
/**********************************************************************************************************************************/
/**
 * @brief Set the PWM Period and Duty Cycle
 *
 * @param period The PWM period
 * @param duty The PWM duty cycle
 * @return bool true if success, false otherwise
*/
bool ASIM::setPWM(uint8_t channel, uint16_t period, uint8_t duty) {
	char pwm_cmd[20];
	DEBUG_PRINTLN(F("================= SET PWM ================="));
	if(period > 2000) {
		DEBUG_PRINTLN(F("PWM PERIOD CAN NOT EXCEED 2000"));
		return SIM_FAILED;
	}
	if(duty > 100) {
		DEBUG_PRINTLN(F("PWM DUTY CYCLE CAN NOT EXCEED 100"));
		return SIM_FAILED;
	}

	sprintf(pwm_cmd, "AT+SPWM=%d,%d,%d", channel, period, duty);
	return sendVerifyedCommand(pwm_cmd, ok_reply);
}


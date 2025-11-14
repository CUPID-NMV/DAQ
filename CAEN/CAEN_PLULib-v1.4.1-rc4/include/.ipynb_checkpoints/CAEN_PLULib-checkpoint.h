#ifndef _CAEN_PLU_H
#define _CAEN_PLU_H

#include <stdint.h>
#ifdef _WIN32
#define CAEN_PLU_API __stdcall
#define CAEN_PLU_DLLAPI
#else
#define CAEN_PLU_API
#define CAEN_PLU_DLLAPI __attribute__((visibility("default")))
#endif

#define CAEN_PLU_STR_HELPER(S)				#S
#define CAEN_PLU_STR(S)						CAEN_PLU_STR_HELPER(S)

/*!
* @defgroup VersionMacros Version macros
* @brief Macros to define the library version.
* @{ */
#define CAEN_PLU_VERSION_MAJOR		1																														//!< Major version
#define CAEN_PLU_VERSION_MINOR		4																														//!< Minor version
#define CAEN_PLU_VERSION_PATCH		1																														//!< Patch version
#define CAEN_PLU_VERSION			CAEN_PLU_STR(CAEN_PLU_VERSION_MAJOR) "." CAEN_PLU_STR(CAEN_PLU_VERSION_MINOR) "." CAEN_PLU_STR(CAEN_PLU_VERSION_PATCH)	//!< The version string as Major.Minor.Patch
#define CAEN_PLU_VERSION_NUMBER		((CAEN_PLU_VERSION_MAJOR) * 10000 + (CAEN_PLU_VERSION_MINOR) * 100 + (CAEN_PLU_VERSION_PATCH))							//!< The version number: for example version 1.2.3 gives 10203
/*! @} */

/********************************************************
*                    TYPES                              *
********************************************************/

/*! \defgroup ErrorCodes Error codes
*  \brief Macros to define error codes from library functions
*  \@{
*  \{
*/
typedef enum CAEN_PLU_ERROR_CODE
{
	CAEN_PLU_OK								=  0,
	CAEN_PLU_GENERIC						= -1,
	CAEN_PLU_INTERFACE						= -2,
	CAEN_PLU_FPGA							= -3,
	CAEN_PLU_TRANSFER_MAX_LENGTH			= -4,
	CAEN_PLU_NOTCONNECTED					= -5,
	CAEN_PLU_NO_DATA_AVAILABLE				= -6,
	CAEN_PLU_TOO_MANY_DEVICES_CONNECTED		= -7,
	CAEN_PLU_INVALID_HANDLE					= -8,
	CAEN_PLU_INVALID_HARDWARE				= -9,
	CAEN_PLU_INVALID_PARAMETERS				= -10,
	CAEN_PLU_TERMINATED						= -13,
} CAEN_PLU_ERROR_CODE;
#
/* \}@ */

//! Misc definitions
#define		GET_VERSION						1
#define		ERASE_FLASH						2
#define		UPLOAD_FW						3
#define		READ_CRC						4
#define		RUN_APPLICATION					5
#define		ERASE_CONFIG					0x10
#define		WRITE_CONFIG					0x11
#define		READ_CONFIG						0x12
#define		REBOOT_BOARD					0xFF

//! Bootloader-related definitions
#define		CAEN_PLU_PIC32_BOOTLOADER		0x01
#define		CAEN_PLU_DPP_ETHERNET			0x02
#define		CAEN_PLU_EMULATOR_ETHERNET		0x03

//! Maximum number of devices that can be connected at the same time
#define		MAX_NUMBER_OF_DEVICE			100

/** @brief USB device descriptor */
typedef struct _tUSBDevice
{
	uint32_t	id;
	char		SN[64];
	char		DESC[64];
} tUSBDevice;


/** @brief Ethernet Board configuration */
typedef struct _tETHBOARDConfig
{
	uint8_t		IPAddress[4];
	uint8_t		NETMask[4];
	uint8_t		IPGateway[4];
	uint8_t		PriDNS[4];
	uint8_t		MAC[6];
	uint32_t	UDPPort;
	uint32_t	HW_Code_Number;
	uint8_t		CompanyInfo[256];
	uint8_t		FriendlyName[128];
	uint32_t	HW_Revision;
	uint32_t	HW_Bootloader_Delay;
	uint8_t		HW_SerialNumber[8];
	uint32_t	TCPPort;
} tETHBOARDConfig;

/** @brief Generic Board informations (Configuration ROM) */
typedef struct _tBOARDInfo
{
	uint32_t	checksum;
	uint32_t	checksum_length2;
	uint32_t	checksum_length1;
	uint32_t	checksum_length0;
	uint32_t	checksum_constant2;
	uint32_t	checksum_constant1;
	uint32_t	checksum_constant0;
	uint32_t	c_code;
	uint32_t	r_code;
	uint32_t	oui2;
	uint32_t	oui1;
	uint32_t	oui0;
	uint32_t	version;
	uint32_t	board2;
	uint32_t	board1;
	uint32_t	board0;
	uint32_t	revis3;
	uint32_t	revis2;
	uint32_t	revis1;
	uint32_t	revis0;
	uint32_t	reserved[8];
	uint32_t	sernum0_v2;
	uint32_t	sernum1_v2;
	uint32_t	sernum2_v2;
	uint32_t	sernum3_v2;
	uint32_t	sernum1;
	uint32_t	sernum0;
} tBOARDInfo;


/** @brief Possible target FPGA */
typedef enum
{
	FPGA_MAIN  = 0,
	FPGA_USER  = 1,
	FPGA_DELAY = 2,
} t_FPGA_V2495;

/** @brief Possible target FPGA */
typedef enum
{
	CAEN_PLU_CONNECT_DIRECT_USB = 0,
	CAEN_PLU_CONNECT_DIRECT_ETH = 1,
	CAEN_PLU_CONNECT_VME_V1718 = 2,
	CAEN_PLU_CONNECT_VME_V2718 = 3,
	CAEN_PLU_CONNECT_VME_V4718_ETH = 4,
	CAEN_PLU_CONNECT_VME_V4718_USB = 5,
	CAEN_PLU_CONNECT_VME_A4818 = 6,
} t_ConnectionModes;


#ifdef __cplusplus
extern "C" {
#endif

/**
 * \defgroup Core Core functions
 * @{
*/

/*!
* Connect to a device. The device should be closed using CAEN_PLU_CloseDevice().
* \param[in]	connection_mode							the connection type
* \param[in]	IPAddress_or_SN_or_VMEBaseAddress		IP Address (ethernet) or Serial Number (USB) or Base address (VME)
* \param[in]	VMElink									Link number (VME). In case of USB connection, this paramenter is not significant.
* \param[in]	VMEConetNode							Conet node (VME with V2718 bridge). In case of USB connection or VME V1718, this paramenter is not significant.
* \param[out]	handle									Returns an handle for subsequent library accesses. Can be NULL if connection is not possible.
* \return		::CAEN_PLU_OK (0) in case of success. Error codes specified in #CAEN_PLU_ERROR_CODE.
* \ingroup Core
*/
CAEN_PLU_DLLAPI CAEN_PLU_ERROR_CODE CAEN_PLU_API CAEN_PLU_OpenDevice(t_ConnectionModes connection_mode, const char *IPAddress_or_SN_or_VMEBaseAddress, int VMElink, int VMEConetNode, int *handle);

/*!
* Connect to a device. The device should be closed using CAEN_PLU_CloseDevice().
* \param[in]	connection_mode							the connection type
* \param[in]	IPAddress_or_SN_or_VMELink				IP Address (ethernet/V4718_ETH) or Serial Number (USB DIRECT or V4718_USB) or Link number (VME)
* \param[in]	VMEConetNode							Conet node (VME with V2718 bridge). In case of USB connection or VME V1718, this paramenter is not significant.
* \param[in]	VMEBaseAddress							Base address (VME). In case of Direct connection, this paramenter is not significant.
* \param[out]	handle									Returns an handle for subsequent library accesses. Can be NULL if connection is not possible.
* \return		::CAEN_PLU_OK (0) in case of success.	Error codes specified in #CAEN_PLU_ERROR_CODE.
* \ingroup Core
*/
CAEN_PLU_DLLAPI CAEN_PLU_ERROR_CODE CAEN_PLU_API CAEN_PLU_OpenDevice2(t_ConnectionModes connection_mode, const void* IPAddress_or_SN_or_VMELink, int VMEConetNode, const char* VMEBaseAddress, int* handle);

/*!
* Disconnect from a device.
* \param[out]	handle									Returns an handle for subsequent library accesses. Can be NULL if connection is not possible.
* \return		::CAEN_PLU_ERROR_CODE
* \ingroup Core
*/
CAEN_PLU_DLLAPI CAEN_PLU_ERROR_CODE CAEN_PLU_API CAEN_PLU_CloseDevice(int handle);

/*!
* Write a 3
* \param[in]	handle									Library handle (as returned by CAEN_PLU_OpenDevice())
* \param[in]	address									Register address
* \param[in]	value									Data to write into target register
* \return		::CAEN_PLU_ERROR_CODE
* \ingroup Core
*/
CAEN_PLU_DLLAPI CAEN_PLU_ERROR_CODE CAEN_PLU_API CAEN_PLU_WriteReg(int handle, uint32_t address, uint32_t value);

/*!
* Read a 32-bit register
* \param[in]	handle									Library handle (as returned by CAEN_PLU_OpenDevice())
* \param[in]	address									Register address
* \param[out]	value									Register content
* \return		::CAEN_PLU_ERROR_CODE
* \ingroup Core
*/
CAEN_PLU_DLLAPI CAEN_PLU_ERROR_CODE CAEN_PLU_API CAEN_PLU_ReadReg(int handle, uint32_t address, uint32_t *value);

/*!
* Write 32-bit data
* \param[in]	handle									Library handle (as returned by CAEN_PLU_OpenDevice())
* \param[in]	start_address							Start address for write operation. Address is autotically increased by 4 for each new value access.
* \param[in]	size									Size of transfer in 32-bit words
* \param[out]	value									Pointer to values to write.
* \return		::CAEN_PLU_ERROR_CODE
* \ingroup Core
*/
CAEN_PLU_DLLAPI CAEN_PLU_ERROR_CODE CAEN_PLU_API CAEN_PLU_WriteData32(int handle, uint32_t start_address, uint32_t size, const uint32_t *value);

/*!
* Write 32-bit data at the same address (FIFO mode)
* \param[in]	handle									Library handle (as returned by CAEN_PLU_OpenDevice())
* \param[in]	start_address							Start adddress for write operation. Address is **NOT** incremented for each new value access.
* \param[in]	size									Size of transfer in 32-bit words
* \param[out]	value									Pointer to values to write.
* \return		::CAEN_PLU_ERROR_CODE
* \ingroup Core
*/
CAEN_PLU_DLLAPI CAEN_PLU_ERROR_CODE CAEN_PLU_API CAEN_PLU_WriteFIFO32(int handle, uint32_t start_address, uint32_t size, const uint32_t *value);

/*!
* Read 32-bit data
* \param[in]	handle									Library handle (as returned by CAEN_PLU_OpenDevice())
* \param[in]	start_address							Start adddress for read operation. Address automatically incremented for each new value access.
* \param[in]	size									Size of transfer in 32-bit words.
* \param[out]	value									Pointer to read values.
* \param[out]	nw										Number of 32-bit words read. it is less or equal to size.
* \return		::CAEN_PLU_ERROR_CODE
* \ingroup Core
*/
CAEN_PLU_DLLAPI CAEN_PLU_ERROR_CODE CAEN_PLU_API CAEN_PLU_ReadData32(int handle, uint32_t start_address, uint32_t size, uint32_t *value, uint32_t *nw);

/*!
* Read 32-bit data from the same adddress (FIFO mode)
* \param[in]	handle									Library handle (as returned by CAEN_PLU_OpenDevice())
* \param[in]	address									Start adddress for read operation. Address is **NOT** incremented for each new value access.
* \param[in]	size									Size of transfer in 32-bit words.
* \param[out]	value									Pointer to read values.
* \param[out]	nw										Number of 32-bit words read. it is less or equal to size.
* \return		::CAEN_PLU_ERROR_CODE
* \ingroup Core
*/
CAEN_PLU_DLLAPI CAEN_PLU_ERROR_CODE CAEN_PLU_API CAEN_PLU_ReadFIFO32(int handle, uint32_t address, uint32_t size, uint32_t *value, uint32_t *nw);
/**@}*/

/**
* \defgroup Enumerate Enumeration functions
* @{
*/
/*!
* Enumerate boards connected via USB direct link
* \param[out]	pvArg1									Pointer to USB devices enumerated.
* \param[out]	numDevs									Number of enumerated boards.
* \return		::CAEN_PLU_ERROR_CODE
* \ingroup Enumerate
*/
CAEN_PLU_DLLAPI CAEN_PLU_ERROR_CODE CAEN_PLU_API CAEN_PLU_USBEnumerate(tUSBDevice *pvArg1, uint32_t *numDevs);

/*!
* Enumerate boards connected via USB direct link and returns a Serial Number as a string
* \param[out]	numDevs									Number of enumerated devices
* \param[out]	DeviceSNs								Serial number
* \param[in]	buffersize								String length
* \return		::CAEN_PLU_ERROR_CODE
* \ingroup Enumerate
*/

CAEN_PLU_DLLAPI CAEN_PLU_ERROR_CODE CAEN_PLU_API CAEN_PLU_USBEnumerateSerialNumber(unsigned int *numDevs, char *DeviceSNs, uint32_t buffersize);


/**@}*/

/**
* \defgroup GateAndDelay Gate and Delay generators functions
* @{
*/

/*!
* Gate and Delay initialization. It **MUST** be called prior to any Gate and Delay function call.
* \param[in]	handle									Library handle (as returned by CAEN_PLU_OpenDevice())
* \return		::CAEN_PLU_ERROR_CODE
* \ingroup GateAndDelay
*/
CAEN_PLU_DLLAPI CAEN_PLU_ERROR_CODE CAEN_PLU_API CAEN_PLU_InitGateAndDelayGenerators(int handle);

/*!
* Enables and set a single gate and delay generator channel.
* \param[in]	handle									Library handle (as returned by CAEN_PLU_OpenDevice())
* \param[in]	channel									Gate and Delay channel to set
* \param[in]	gate									Gate value in gate steps (Valid range = 0-65535)
* \param[in]	delay									Delay value in delay steps (Valid range = 0-65535)
* \param[in]	scale_factor							Scale factor for delay (Valid range = 0-255). 0=minimum gate and delay resolution (~10 ns)
* \param[in]	enable									Channel enable
* \return		::CAEN_PLU_ERROR_CODE
* \note **Gate+Delay cannot exceed 65535**
* \ingroup GateAndDelay
*/
CAEN_PLU_DLLAPI CAEN_PLU_ERROR_CODE CAEN_PLU_API CAEN_PLU_SetGateAndDelayGenerator(int handle, uint32_t channel, uint32_t enable, uint32_t gate, uint32_t delay, uint32_t scale_factor);

/*!
* Enables and set **ALL** gate and delay generators channel with a common value.
* \param[in]	handle									Library handle (as returned by CAEN_PLU_OpenDevice())
* \param[in]	gate									Gate value in gate steps (Valid range = 0-65535)
* \param[in]	delay									Delay value in delay steps (Valid range = 0-65535)
* \param[in]	scale_factor							Scale factor for delay (Valid range = 0-255). 0=minimum gate and delay resolution (~10 ns)
* \return		::CAEN_PLU_ERROR_CODE
* \note **Gate+Delay cannot exceed 65535**
* \ingroup GateAndDelay
*/
CAEN_PLU_DLLAPI CAEN_PLU_ERROR_CODE CAEN_PLU_API CAEN_PLU_SetGateAndDelayGenerators(int handle, uint32_t gate, uint32_t delay, uint32_t scale_factor);

/*!
* Get Gate and delay channel parameters
* \param[in]	handle									Library handle (as returned by CAEN_PLU_OpenDevice())
* \param[in]	channel									Gate and Delay channel
* \param[out]	gate										Gate value in gate steps
* \param[out]	delay									Delay value in delay steps
* \param[out]	scale_factor								Fine tune value in fine tune steps
* \return		::CAEN_PLU_ERROR_CODE
* \ingroup GateAndDelay
*/
CAEN_PLU_DLLAPI CAEN_PLU_ERROR_CODE CAEN_PLU_API CAEN_PLU_GetGateAndDelayGenerator(int handle, uint32_t channel, uint32_t *gate, uint32_t *delay, uint32_t *scale_factor);
/**@}*/

/**
* \defgroup Flash  Low Level Flash memory access functions
* @{
*/

/*!
* Flash access enable. It **MUST** be called prior to any flash access function call.
* \param[in]	handle									Library handle (as returned by CAEN_PLU_OpenDevice())
* \param[in]	FPGA									::t_FPGA_V2495
* \return		::CAEN_PLU_ERROR_CODE
* \ingroup Flash
*/
CAEN_PLU_DLLAPI CAEN_PLU_ERROR_CODE CAEN_PLU_API CAEN_PLU_EnableFlashAccess(int handle, t_FPGA_V2495 FPGA);

/*!
* Flash access disable. It **MUST** be called prior to any flash access function call.
* \param[in]	handle									Library handle (as returned by CAEN_PLU_OpenDevice())
* \param[in]	FPGA									::t_FPGA_V2495
* \return		::CAEN_PLU_ERROR_CODE
* \ingroup Flash
*/
CAEN_PLU_DLLAPI CAEN_PLU_ERROR_CODE CAEN_PLU_API CAEN_PLU_DisableFlashAccess(int handle, t_FPGA_V2495 FPGA);

/*!
* Delete a single flash sector
* \param[in]	handle									Library handle (as returned by CAEN_PLU_OpenDevice())
* \param[in]	FPGA									::t_FPGA_V2495
* \param[in]	sector									Flash sector to delete (64 KB). MAIN and USER flash (N25Q256 model) have 512x64KB sectors. DELAY flash (W25Q64 model) has 128x64KB sectors.
														Please **be aware** that some sectors are reserved for factory and user firmware.
														User storage area is in sectors 106-510 for MAIN flash and sectors 458-510 for USER flash. DELAY flash should not be used for user data.
														See flash documentation for further informations.
* \return		::CAEN_PLU_ERROR_CODE
* \ingroup Flash
*/
CAEN_PLU_DLLAPI CAEN_PLU_ERROR_CODE CAEN_PLU_API CAEN_PLU_DeleteFlashSector(int handle, t_FPGA_V2495 FPGA, uint32_t sector);

/*!
* Write data into flash
* \param[in]	handle									Library handle (as returned by CAEN_PLU_OpenDevice())
* \param[in]	FPGA									::t_FPGA_V2495
* \param[in]	address									Flash start address
* \param[in]	data									Pointer to data to write into flash
* \param[in]	length									Data length in 32-bit words
* \return		::CAEN_PLU_ERROR_CODE
* \ingroup Flash
*/
CAEN_PLU_DLLAPI CAEN_PLU_ERROR_CODE CAEN_PLU_API CAEN_PLU_WriteFlashData(int handle, t_FPGA_V2495 FPGA, uint32_t address, uint32_t *data, uint32_t length);

/*!
* Read data from flash
* \param[in]	handle									Library handle (as returned by CAEN_PLU_OpenDevice())
* \param[in]	FPGA										::t_FPGA_V2495
* \param[in]	address									Flash start address
* \param[out]	data										Pointer to data read from flash
* \param[in]	length									Data length in 32-bit words
* \return		::CAEN_PLU_ERROR_CODE
* \ingroup Flash
*/
CAEN_PLU_DLLAPI CAEN_PLU_ERROR_CODE CAEN_PLU_API CAEN_PLU_ReadFlashData(int handle, t_FPGA_V2495 FPGA, uint32_t address, uint32_t *data,	uint32_t length);
/**@}*/

/**
* \defgroup Misc  Misc functions
* @{
*/
/*!
* Retrieve module informations
* \param[in]	handle									Library handle (as returned by CAEN_PLU_OpenDevice())
* \param[out]	HWOPTIONS								Pointer to a ::tBOARDInfo structure
* \return		::CAEN_PLU_ERROR_CODE
* \ingroup Misc
*/
CAEN_PLU_DLLAPI CAEN_PLU_ERROR_CODE CAEN_PLU_API CAEN_PLU_GetInfo(int handle, tBOARDInfo *HWOPTIONS);

/*!
* Retrieve module serial number stored into configuration rom
* \param[in]	handle									Library handle (as returned by CAEN_PLU_OpenDevice())
* \param[out]	sn										Serial number
* \param[in]	buffersize								Serial number string length
* \return		::CAEN_PLU_ERROR_CODE
* \ingroup Misc
*/
CAEN_PLU_DLLAPI CAEN_PLU_ERROR_CODE CAEN_PLU_API CAEN_PLU_GetSerialNumber(int handle, char *sn, uint32_t buffersize);
/*!
* Get current connection status
* \param[in]	handle									Library handle (as returned by CAEN_PLU_OpenDevice())
* \param[out]	status									connection status with ID (0 = USB, 1 = ETH, 2 = V1718, 3 = V2718)
* \return		::CAEN_PLU_ERROR_CODE
* \ingroup Misc
*/
CAEN_PLU_DLLAPI CAEN_PLU_ERROR_CODE CAEN_PLU_API CAEN_PLU_ConnectionStatus(int handle, int *status);
/**@}*/

#ifdef __cplusplus
}
#endif

#endif // _CAEN_PLU_H

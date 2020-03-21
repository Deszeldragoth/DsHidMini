#include "Driver.h"
#include "DsHidMiniDrv.tmh"
#include <libconfig.h>


PWSTR G_DsHidMini_Strings[] =
{
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	L"DsHidMini Device" // TODO: populate properly
};

#define MAXIMUM_STRING_LENGTH           (126 * sizeof(WCHAR))
#define DSHIDMINI_DEVICE_STRING          L"DS3 Compatible HID Device"
#define DSHIDMINI_MANUFACTURER_STRING    L"Nefarius Software Solutions e.U."
#define DSHIDMINI_PRODUCT_STRING         L"DS3 Compatible HID Device"
#define DSHIDMINI_SERIAL_NUMBER_STRING   L"TODO: use device address"
#define DSHIDMINI_DEVICE_STRING_INDEX    5


// This macro declares the following function:
// DMF_CONTEXT_GET()
//
DMF_MODULE_DECLARE_CONTEXT(DsHidMini)

// This macro declares the following function:
// DMF_CONFIG_GET()
//
DMF_MODULE_DECLARE_CONFIG(DsHidMini)



//
// Bootstrap DMF initialization
// 
#pragma code_seg("PAGE")
_IRQL_requires_max_(PASSIVE_LEVEL)
_Must_inspect_result_
NTSTATUS
DMF_DsHidMini_Create(
	_In_ WDFDEVICE Device,
	_In_ DMF_MODULE_ATTRIBUTES* DmfModuleAttributes,
	_In_ WDF_OBJECT_ATTRIBUTES* ObjectAttributes,
	_Out_ DMFMODULE* DmfModule
)
{
	NTSTATUS ntStatus;
	DMF_MODULE_DESCRIPTOR dsHidMiniDesc;
	DMF_CALLBACKS_DMF dsHidMiniCallbacks;
	PDEVICE_CONTEXT pDevCtx;

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DSHIDMINIDRV, "%!FUNC! Entry");

	pDevCtx = DeviceGetContext(Device);

	//
	// Set defaults
	// 
	DS_DRIVER_CONFIGURATION_INIT_DEFAULTS(&pDevCtx->Configuration);

	/*
	 * TODO: BEGIN clean me up!
	 */
	config_setting_t* root, * setting, * dev;
	config_t cfg;

	config_init(&cfg);

	if (!config_read_file(&cfg, DS_DRIVER_CFG_FILE_PATH))
	{
		TraceEvents(TRACE_LEVEL_ERROR,
			TRACE_DSHIDMINIDRV,
			"Failed to load configuration: %s:%d - %s",
			config_error_file(&cfg),
			config_error_line(&cfg),
			config_error_text(&cfg)
		);

		root = config_root_setting(&cfg);
		setting = config_setting_get_member(root, "Devices");
		if (!setting)
			setting = config_setting_add(root, "Devices", CONFIG_TYPE_LIST);

		dev = config_setting_add(setting, NULL, CONFIG_TYPE_GROUP);

		PWSTR wideBuf = WdfMemoryGetBuffer(pDevCtx->InstanceId, NULL);
		PSTR buf = malloc(200);
		wcstombs(buf, wideBuf, 200);

		setting = config_setting_add(dev, "InstanceId", CONFIG_TYPE_STRING);
		config_setting_set_string(setting, buf);
		free(buf);

		config_write_file(&cfg, DS_DRIVER_CFG_FILE_PATH);
	}
	else
	{
		setting = config_lookup(&cfg, "Devices");
		if (setting != NULL)
		{
			unsigned int count = config_setting_length(setting);
			unsigned int i;

			PWSTR wideBuf = WdfMemoryGetBuffer(pDevCtx->InstanceId, NULL);
			PSTR buf = malloc(200);
			wcstombs(buf, wideBuf, 200);

			TraceEvents(TRACE_LEVEL_INFORMATION,
				TRACE_DSHIDMINIDRV,
				"!! %d %s",
				count, buf
			);

			for (i = 0; i < count; ++i)
			{
				dev = config_setting_get_elem(setting, i);

				const char* instId;

				if (config_setting_lookup_string(dev, "InstanceId", &instId)
					&& strcmp(instId, buf) == 0)
				{
					TraceEvents(TRACE_LEVEL_INFORMATION,
						TRACE_DSHIDMINIDRV,
						"!! Found config for instanceId: %s",
						buf
					);

					config_setting_lookup_int(
						dev,
						"HidDeviceMode",
						(int*)&pDevCtx->Configuration.HidDeviceMode);
					config_setting_lookup_bool(
						dev,
						"MuteDigitalPressureButtons",
						(int*)&pDevCtx->Configuration.MuteDigitalPressureButtons);
					config_setting_lookup_int(
						dev,
						"VendorId",
						(int*)&pDevCtx->Configuration.VendorId);
					config_setting_lookup_int(
						dev,
						"ProductId",
						(int*)&pDevCtx->Configuration.ProductId);
					config_setting_lookup_int(
						dev,
						"VersionNumber",
						(int*)&pDevCtx->Configuration.VersionNumber);

					break;
				}
			}

			free(buf);
		}
	}

	config_destroy(&cfg);
	/*
	 * TODO: END clean me up!
	 */

	DMF_CALLBACKS_DMF_INIT(&dsHidMiniCallbacks);
	dsHidMiniCallbacks.ChildModulesAdd = DMF_DsHidMini_ChildModulesAdd;
	dsHidMiniCallbacks.DeviceOpen = DMF_DsHidMini_Open;
	dsHidMiniCallbacks.DeviceClose = DMF_DsHidMini_Close;

	DMF_MODULE_DESCRIPTOR_INIT_CONTEXT_TYPE(dsHidMiniDesc,
		DsHidMini,
		DMF_CONTEXT_DsHidMini,
		DMF_MODULE_OPTIONS_PASSIVE,
		DMF_MODULE_OPEN_OPTION_OPEN_PrepareHardware);

	dsHidMiniDesc.CallbacksDmf = &dsHidMiniCallbacks;

	ntStatus = DMF_ModuleCreate(Device,
		DmfModuleAttributes,
		ObjectAttributes,
		&dsHidMiniDesc,
		DmfModule);
	if (!NT_SUCCESS(ntStatus))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DSHIDMINIDRV, "DMF_ModuleCreate fails: ntStatus=%!STATUS!", ntStatus);
		goto Exit;
	}

Exit:

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DSHIDMINIDRV, "%!FUNC! Exit");

	return(ntStatus);
}
#pragma code_seg()

//
// Bootstraps VirtualHidMini DMF module
// 
#pragma code_seg("PAGE")
_Function_class_(DMF_ChildModulesAdd)
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
DMF_DsHidMini_ChildModulesAdd(
	_In_ DMFMODULE DmfModule,
	_In_ DMF_MODULE_ATTRIBUTES* DmfParentModuleAttributes,
	_In_ PDMFMODULE_INIT DmfModuleInit
)
{
	DMF_MODULE_ATTRIBUTES moduleAttributes;
	DMF_CONTEXT_DsHidMini* moduleContext;
	DMF_CONFIG_VirtualHidMini vHidCfg;
	PDEVICE_CONTEXT pDevCtx;

	PAGED_CODE();

	UNREFERENCED_PARAMETER(DmfParentModuleAttributes);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DSHIDMINIDRV, "%!FUNC! Entry");

	moduleContext = DMF_CONTEXT_GET(DmfModule);
	pDevCtx = DeviceGetContext(DMF_ParentDeviceGet(DmfModule));

	DMF_CONFIG_VirtualHidMini_AND_ATTRIBUTES_INIT(&vHidCfg,
		&moduleAttributes);

	vHidCfg.VendorId = pDevCtx->Configuration.VendorId;
	vHidCfg.ProductId = pDevCtx->Configuration.ProductId;
	vHidCfg.VersionNumber = pDevCtx->Configuration.VersionNumber;

	switch (pDevCtx->Configuration.HidDeviceMode)
	{
	case DsHidMiniDeviceModeSingle:

		vHidCfg.HidDescriptor = &G_Ds3HidDescriptor_Single_Mode;
		vHidCfg.HidDescriptorLength = sizeof(G_Ds3HidDescriptor_Single_Mode);
		vHidCfg.HidReportDescriptor = G_Ds3HidReportDescriptor_Single_Mode;
		vHidCfg.HidReportDescriptorLength = G_Ds3HidDescriptor_Single_Mode.DescriptorList[0].wReportLength;

		break;
	case DsHidMiniDeviceModeMulti:

		vHidCfg.HidDescriptor = &G_Ds3HidDescriptor_Split_Mode;
		vHidCfg.HidDescriptorLength = sizeof(G_Ds3HidDescriptor_Split_Mode);
		vHidCfg.HidReportDescriptor = G_Ds3HidReportDescriptor_Split_Mode;
		vHidCfg.HidReportDescriptorLength = G_Ds3HidDescriptor_Split_Mode.DescriptorList[0].wReportLength;

		break;
	case DsHidMiniDeviceModeSixaxisCompatible:

		vHidCfg.HidDescriptor = &G_SixaxisHidDescriptor;
		vHidCfg.HidDescriptorLength = sizeof(G_SixaxisHidDescriptor);
		vHidCfg.HidReportDescriptor = G_SixaxisHidReportDescriptor;
		vHidCfg.HidReportDescriptorLength = G_SixaxisHidDescriptor.DescriptorList[0].wReportLength;
		
		break;
	default:
		TraceEvents(TRACE_LEVEL_ERROR,
			TRACE_DSHIDMINIDRV,
			"Unknown HID Device Mode: 0x%02X", pDevCtx->Configuration.HidDeviceMode);
		return;
	}

	vHidCfg.HidDeviceAttributes.VendorID = pDevCtx->Configuration.VendorId;
	vHidCfg.HidDeviceAttributes.ProductID = pDevCtx->Configuration.ProductId;
	vHidCfg.HidDeviceAttributes.VersionNumber = pDevCtx->Configuration.VersionNumber;
	vHidCfg.HidDeviceAttributes.Size = sizeof(HID_DEVICE_ATTRIBUTES);

	vHidCfg.GetInputReport = DsHidMini_GetInputReport;
	vHidCfg.GetFeature = DsHidMini_GetFeature;
	vHidCfg.SetFeature = DsHidMini_SetFeature;
	vHidCfg.SetOutputReport = DsHidMini_SetOutputReport;
	vHidCfg.WriteReport = DsHidMini_WriteReport;

	vHidCfg.StringSizeCbManufacturer = sizeof(DSHIDMINI_MANUFACTURER_STRING);
	vHidCfg.StringManufacturer = DSHIDMINI_MANUFACTURER_STRING;
	vHidCfg.StringSizeCbProduct = sizeof(DSHIDMINI_PRODUCT_STRING);
	vHidCfg.StringProduct = DSHIDMINI_PRODUCT_STRING;
	vHidCfg.StringSizeCbSerialNumber = sizeof(DSHIDMINI_SERIAL_NUMBER_STRING);
	vHidCfg.StringSerialNumber = DSHIDMINI_SERIAL_NUMBER_STRING;

	vHidCfg.Strings = G_DsHidMini_Strings;
	vHidCfg.NumberOfStrings = ARRAYSIZE(G_DsHidMini_Strings);

	DMF_DmfModuleAdd(DmfModuleInit,
		&moduleAttributes,
		WDF_NO_OBJECT_ATTRIBUTES,
		&moduleContext->DmfModuleVirtualHidMini);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DSHIDMINIDRV, "%!FUNC! Exit");
}
#pragma code_seg()

#pragma code_seg("PAGE")
_Function_class_(DMF_Open)
_IRQL_requires_max_(PASSIVE_LEVEL)
_Must_inspect_result_
static
NTSTATUS
DMF_DsHidMini_Open(
	_In_ DMFMODULE DmfModule
)
{
	NTSTATUS			status = STATUS_SUCCESS;

	UNREFERENCED_PARAMETER(DmfModule);

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DSHIDMINIDRV, "%!FUNC! Entry");



	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DSHIDMINIDRV, "%!FUNC! Exit");

	return status;
}
#pragma code_seg()

#pragma code_seg("PAGE")
_Function_class_(DMF_Close)
_IRQL_requires_max_(PASSIVE_LEVEL)
static
VOID
DMF_DsHidMini_Close(
	_In_ DMFMODULE DmfModule
)
{
	PDEVICE_CONTEXT pDevCtx;
	PDS_DRIVER_CONFIGURATION pCfg;
	config_t cfg;
	config_setting_t* dev, * setting;

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DSHIDMINIDRV, "%!FUNC! Entry");

	pDevCtx = DeviceGetContext(DMF_ParentDeviceGet(DmfModule));
	pCfg = &pDevCtx->Configuration;


	/*
	 * TODO: BEGIN clean me up!
	 */

	config_init(&cfg);

	if (!config_read_file(&cfg, DS_DRIVER_CFG_FILE_PATH))
	{
		TraceEvents(TRACE_LEVEL_ERROR,
			TRACE_DSHIDMINIDRV,
			"Failed to load configuration: %s:%d - %s",
			config_error_file(&cfg),
			config_error_line(&cfg),
			config_error_text(&cfg)
		);
	}
	else
	{
		setting = config_lookup(&cfg, "Devices");
		if (setting != NULL)
		{
			unsigned int count = config_setting_length(setting);
			unsigned int i;

			PWSTR wideBuf = WdfMemoryGetBuffer(pDevCtx->InstanceId, NULL);
			PSTR buf = malloc(200);
			wcstombs(buf, wideBuf, 200);

			TraceEvents(TRACE_LEVEL_INFORMATION,
				TRACE_DSHIDMINIDRV,
				"!! %d %s",
				count, buf
			);

			for (i = 0; i < count; ++i)
			{
				dev = config_setting_get_elem(setting, i);

				const char* instId;

				if (config_setting_lookup_string(dev, "InstanceId", &instId)
					&& strcmp(instId, buf) == 0)
				{
					TraceEvents(TRACE_LEVEL_INFORMATION,
						TRACE_DSHIDMINIDRV,
						"!! Found config for instanceId: %s",
						buf
					);

					//
					// HidDeviceMode
					// 
					setting = config_setting_get_member(dev, "HidDeviceMode");
					if (!setting)
						setting = config_setting_add(dev, "HidDeviceMode", CONFIG_TYPE_INT);
					config_setting_set_int(setting, pCfg->HidDeviceMode);

					//
					// MuteDigitalPressureButtons
					// 
					setting = config_setting_get_member(dev, "MuteDigitalPressureButtons");
					if (!setting)
						setting = config_setting_add(dev, "MuteDigitalPressureButtons", CONFIG_TYPE_BOOL);
					config_setting_set_int(setting, pCfg->MuteDigitalPressureButtons);

					//
					// VendorId
					// 
					setting = config_setting_get_member(dev, "VendorId");
					if (!setting)
						setting = config_setting_add(dev, "VendorId", CONFIG_TYPE_INT);
					config_setting_set_int(setting, pCfg->VendorId);

					//
					// ProductId
					// 
					setting = config_setting_get_member(dev, "ProductId");
					if (!setting)
						setting = config_setting_add(dev, "ProductId", CONFIG_TYPE_INT);
					config_setting_set_int(setting, pCfg->ProductId);

					//
					// VersionNumber
					// 
					setting = config_setting_get_member(dev, "VersionNumber");
					if (!setting)
						setting = config_setting_add(dev, "VersionNumber", CONFIG_TYPE_INT);
					config_setting_set_int(setting, pCfg->VersionNumber);

					break;
				}
			}

			free(buf);
		}
	}
	/*
	 * TODO: END clean me up!
	 */

	config_write_file(&cfg, DS_DRIVER_CFG_FILE_PATH);

	config_destroy(&cfg);
	
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DSHIDMINIDRV, "%!FUNC! Exit");
}
#pragma code_seg()

NTSTATUS
DsHidMini_GetInputReport(
	_In_ DMFMODULE DmfModule,
	_In_ WDFREQUEST Request,
	_In_ HID_XFER_PACKET* Packet,
	_Out_ ULONG* ReportSize
)
{
	UNREFERENCED_PARAMETER(DmfModule);
	UNREFERENCED_PARAMETER(Request);
	UNREFERENCED_PARAMETER(Packet);
	UNREFERENCED_PARAMETER(ReportSize);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DSHIDMINIDRV, "%!FUNC! Entry");

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DSHIDMINIDRV, "%!FUNC! Exit");

	return STATUS_UNSUCCESSFUL;
}

NTSTATUS

//
// Submit new HID Input Report.
// 
DsHidMini_RetrieveNextInputReport(
	_In_ DMFMODULE DmfModule,
	_In_ WDFREQUEST Request,
	_Out_ UCHAR** Buffer,
	_Out_ ULONG* BufferSize
)
{
	DMFMODULE dmfModuleParent;
	DMF_CONTEXT_DsHidMini* moduleContext;
	PDEVICE_CONTEXT pDevCtx;

	UNREFERENCED_PARAMETER(Request);

	TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DSHIDMINIDRV, "%!FUNC! Entry");

	dmfModuleParent = DMF_ParentModuleGet(DmfModule);
	moduleContext = DMF_CONTEXT_GET(dmfModuleParent);
	pDevCtx = DeviceGetContext(DMF_ParentDeviceGet(DmfModule));

	*Buffer = moduleContext->InputReport;

	switch (pDevCtx->Configuration.HidDeviceMode)
	{
	case DsHidMiniDeviceModeSingle:
	case DsHidMiniDeviceModeMulti:
		*BufferSize = DS3_HID_INPUT_REPORT_SIZE;
		break;
	case DsHidMiniDeviceModeSixaxisCompatible:
		*BufferSize = 12;
		break;
	}	

#ifdef DBG
	DumpAsHex(">> Report", *Buffer, (ULONG)*BufferSize);
#endif

	TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DSHIDMINIDRV, "%!FUNC! Exit");

	return STATUS_SUCCESS;
}

NTSTATUS
DsHidMini_GetFeature(
	_In_ DMFMODULE DmfModule,
	_In_ WDFREQUEST Request,
	_In_ HID_XFER_PACKET* Packet,
	_Out_ ULONG* ReportSize
)
{
	NTSTATUS ntStatus = STATUS_SUCCESS;
	ULONG reportSize = 0;
	PDEVICE_CONTEXT pDevCtx;

	PDS_FEATURE_GET_HOST_BD_ADDR pGetHostAddr = NULL;
	PDS_FEATURE_GET_DEVICE_BD_ADDR pGetDeviceAddr = NULL;
	PDS_FEATURE_GET_CONNECTION_TYPE pGetConnectionType = NULL;

	UNREFERENCED_PARAMETER(Request);

	pDevCtx = DeviceGetContext(DMF_ParentDeviceGet(DmfModule));

	UNREFERENCED_PARAMETER(Request);


	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DSHIDMINIDRV, "%!FUNC! Entry");

	switch (Packet->reportId)
	{
	case DS_FEATURE_TYPE_GET_HOST_BD_ADDR:

		TraceEvents(TRACE_LEVEL_INFORMATION,
			TRACE_DSHIDMINIDRV,
			"<< DS_FEATURE_TYPE_GET_HOST_BD_ADDR"
		);

		if (Packet->reportBufferLen < sizeof(DS_FEATURE_GET_HOST_BD_ADDR))
		{
			ntStatus = STATUS_BUFFER_TOO_SMALL;
			goto Exit;
		}

		pGetHostAddr = (PDS_FEATURE_GET_HOST_BD_ADDR)Packet->reportBuffer;
		pGetHostAddr->HostAddress = pDevCtx->HostAddress;
		reportSize = sizeof(DS_FEATURE_GET_HOST_BD_ADDR) - sizeof(Packet->reportId);

		break;
	case DS_FEATURE_TYPE_GET_DEVICE_BD_ADDR:

		TraceEvents(TRACE_LEVEL_INFORMATION,
			TRACE_DSHIDMINIDRV,
			"<< DS_FEATURE_TYPE_GET_DEVICE_BD_ADDR"
		);

		if (Packet->reportBufferLen < sizeof(DS_FEATURE_GET_DEVICE_BD_ADDR))
		{
			ntStatus = STATUS_BUFFER_TOO_SMALL;
			goto Exit;
		}

		pGetDeviceAddr = (PDS_FEATURE_GET_DEVICE_BD_ADDR)Packet->reportBuffer;
		pGetDeviceAddr->DeviceAddress = pDevCtx->DeviceAddress;
		reportSize = sizeof(DS_FEATURE_GET_DEVICE_BD_ADDR) - sizeof(Packet->reportId);

		break;
	case DS_FEATURE_TYPE_GET_DEVICE_TYPE:

		if (Packet->reportBufferLen < sizeof(DS_FEATURE_GET_DEVICE_TYPE))
		{
			ntStatus = STATUS_BUFFER_TOO_SMALL;
			goto Exit;
		}

		break;
	case DS_FEATURE_TYPE_GET_CONNECTION_TYPE:

		TraceEvents(TRACE_LEVEL_INFORMATION,
			TRACE_DSHIDMINIDRV,
			"<< DS_FEATURE_TYPE_GET_CONNECTION_TYPE"
		);

		if (Packet->reportBufferLen < sizeof(DS_FEATURE_GET_CONNECTION_TYPE))
		{
			ntStatus = STATUS_BUFFER_TOO_SMALL;
			goto Exit;
		}

		pGetConnectionType = (PDS_FEATURE_GET_CONNECTION_TYPE)Packet->reportBuffer;
		pGetConnectionType->ConnectionType = pDevCtx->ConnectionType;
		reportSize = sizeof(DS_FEATURE_GET_CONNECTION_TYPE) - sizeof(Packet->reportId);

		break;
	case DS_FEATURE_TYPE_GET_DEVICE_CONFIG:

		if (Packet->reportBufferLen < sizeof(DS_FEATURE_GET_DEVICE_CONFIG))
		{
			ntStatus = STATUS_BUFFER_TOO_SMALL;
			goto Exit;
		}

		break;
	default:
		break;
	}

	*ReportSize = reportSize;

Exit:

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DSHIDMINIDRV, "%!FUNC! Exit (%!STATUS!)", ntStatus);

	return ntStatus;
}

NTSTATUS
DsHidMini_SetFeature(
	_In_ DMFMODULE DmfModule,
	_In_ WDFREQUEST Request,
	_In_ HID_XFER_PACKET* Packet,
	_Out_ ULONG* ReportSize
)
{
	UNREFERENCED_PARAMETER(DmfModule);
	UNREFERENCED_PARAMETER(Request);
	UNREFERENCED_PARAMETER(Packet);
	UNREFERENCED_PARAMETER(ReportSize);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DSHIDMINIDRV, "%!FUNC! Entry");

	switch (Packet->reportId)
	{
	case DS_FEATURE_TYPE_SET_HOST_BD_ADDR:
		break;
	case DS_FEATURE_TYPE_SET_DEVICE_CONFIG:
		break;
	default:
		break;
	}

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DSHIDMINIDRV, "%!FUNC! Exit");

	return STATUS_SUCCESS;
}

NTSTATUS
DsHidMini_SetOutputReport(
	_In_ DMFMODULE DmfModule,
	_In_ WDFREQUEST Request,
	_In_ HID_XFER_PACKET* Packet,
	_Out_ ULONG* ReportSize
)
{
	UNREFERENCED_PARAMETER(DmfModule);
	UNREFERENCED_PARAMETER(Request);
	UNREFERENCED_PARAMETER(Packet);
	UNREFERENCED_PARAMETER(ReportSize);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DSHIDMINIDRV, "%!FUNC! Entry");

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DSHIDMINIDRV, "%!FUNC! Exit");

	return STATUS_SUCCESS;
}

NTSTATUS
DsHidMini_WriteReport(
	_In_ DMFMODULE DmfModule,
	_In_ WDFREQUEST Request,
	_In_ HID_XFER_PACKET* Packet,
	_Out_ ULONG* ReportSize
)
{
	UNREFERENCED_PARAMETER(DmfModule);
	UNREFERENCED_PARAMETER(Request);
	UNREFERENCED_PARAMETER(Packet);
	UNREFERENCED_PARAMETER(ReportSize);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DSHIDMINIDRV, "%!FUNC! Entry");

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DSHIDMINIDRV, "%!FUNC! Exit");

	return STATUS_SUCCESS;
}

//
// Called when data is available on the USB Interrupt IN pipe.
//  
VOID DsUsb_EvtUsbInterruptPipeReadComplete(
	WDFUSBPIPE  Pipe,
	WDFMEMORY   Buffer,
	size_t      NumBytesTransferred,
	WDFCONTEXT  Context
)
{
	NTSTATUS                status;
	PDEVICE_CONTEXT         pDeviceContext;
	size_t                  rdrBufferLength;
	LPVOID                  rdrBuffer;
	DMFMODULE               dmfModule;
	DMF_CONTEXT_DsHidMini* moduleContext;

	UNREFERENCED_PARAMETER(Pipe);
	UNREFERENCED_PARAMETER(NumBytesTransferred);

	TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DSHIDMINIDRV, "%!FUNC! Entry");

	pDeviceContext = DeviceGetContext(Context);
	dmfModule = (DMFMODULE)pDeviceContext->DsHidMiniModule;
	moduleContext = DMF_CONTEXT_GET(dmfModule);
	rdrBuffer = WdfMemoryGetBuffer(Buffer, &rdrBufferLength);

#ifdef DBG
	DumpAsHex(">> USB", rdrBuffer, (ULONG)rdrBufferLength);
#endif

#pragma region HID Input Report (ID 01) processing

	switch (pDeviceContext->Configuration.HidDeviceMode)
	{
	case DsHidMiniDeviceModeMulti:

		DS3_RAW_TO_SPLIT_HID_INPUT_REPORT_01(
			rdrBuffer,
			moduleContext->InputReport,
			pDeviceContext->Configuration.MuteDigitalPressureButtons
		);

#ifdef DBG
		DumpAsHex(">> MULTI", moduleContext->InputReport, DS3_HID_INPUT_REPORT_SIZE);
#endif

		break;
	case DsHidMiniDeviceModeSingle:

		DS3_RAW_TO_SINGLE_HID_INPUT_REPORT(
			rdrBuffer,
			moduleContext->InputReport,
			pDeviceContext->Configuration.MuteDigitalPressureButtons
		);

#ifdef DBG
		DumpAsHex(">> SINGLE", moduleContext->InputReport, DS3_HID_INPUT_REPORT_SIZE);
#endif

		break;
	default:
		break;
	}

	//
	// Notify new Input Report is available
	// 
	status = DMF_VirtualHidMini_InputReportGenerate(
		moduleContext->DmfModuleVirtualHidMini,
		DsHidMini_RetrieveNextInputReport
	);
	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DSHIDMINIDRV,
			"DMF_VirtualHidMini_InputReportGenerate failed with status %!STATUS!", status);
	}

#pragma endregion

#pragma region HID Input Report (ID 02) processing

	if (pDeviceContext->Configuration.HidDeviceMode == DsHidMiniDeviceModeMulti)
	{
		DS3_RAW_TO_SPLIT_HID_INPUT_REPORT_02(
			rdrBuffer,
			moduleContext->InputReport
		);

		//
		// Notify new Input Report is available
		// 
		status = DMF_VirtualHidMini_InputReportGenerate(
			moduleContext->DmfModuleVirtualHidMini,
			DsHidMini_RetrieveNextInputReport
		);
		if (!NT_SUCCESS(status))
		{
			TraceEvents(TRACE_LEVEL_ERROR, TRACE_DSHIDMINIDRV,
				"DMF_VirtualHidMini_InputReportGenerate failed with status %!STATUS!", status);
		}
	}

#pragma endregion

#pragma region HID Input Report (SIXAXIS compatible) processing

	if (pDeviceContext->Configuration.HidDeviceMode == DsHidMiniDeviceModeSixaxisCompatible)
	{
		DS3_RAW_TO_SIXAXIS_HID_INPUT_REPORT(
			rdrBuffer,
			moduleContext->InputReport
		);

		//
		// Notify new Input Report is available
		// 
		status = DMF_VirtualHidMini_InputReportGenerate(
			moduleContext->DmfModuleVirtualHidMini,
			DsHidMini_RetrieveNextInputReport
		);
		if (!NT_SUCCESS(status))
		{
			TraceEvents(TRACE_LEVEL_ERROR, TRACE_DSHIDMINIDRV,
				"DMF_VirtualHidMini_InputReportGenerate failed with status %!STATUS!", status);
		}
	}

#pragma endregion

	TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DSHIDMINIDRV, "%!FUNC! Exit");
}

//
// Called when data is available on the wireless HID Interrupt channel.
// 
void DsBth_HidInterruptReadRequestCompletionRoutine(
	WDFREQUEST Request,
	WDFIOTARGET Target,
	PWDF_REQUEST_COMPLETION_PARAMS Params,
	WDFCONTEXT Context
)
{
	NTSTATUS                    status;
	PUCHAR                      buffer;
	size_t                      bufferLength;
	PUCHAR						inputBuffer;
	WDF_REQUEST_REUSE_PARAMS    params;
	PDEVICE_CONTEXT				pDeviceContext;
	DMFMODULE                   dmfModule;
	DMF_CONTEXT_DsHidMini* moduleContext;


	TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DSHIDMINIDRV, "%!FUNC! Entry");

	UNREFERENCED_PARAMETER(Target);

	pDeviceContext = (PDEVICE_CONTEXT)Context;
	dmfModule = (DMFMODULE)pDeviceContext->DsHidMiniModule;
	moduleContext = DMF_CONTEXT_GET(dmfModule);

	if (!NT_SUCCESS(Params->IoStatus.Status)
		|| Params->Parameters.Ioctl.Output.Length < BTHPS3_SIXAXIS_HID_INPUT_REPORT_SIZE)
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DSHIDMINIDRV,
			"%!FUNC! failed with status %!STATUS!", Params->IoStatus.Status);

		goto resendRequest;
	}

	buffer = (PUCHAR)WdfMemoryGetBuffer(
		Params->Parameters.Ioctl.Output.Buffer,
		&bufferLength);

#ifdef DBG
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DSHIDMINIDRV, "!! buffer: 0x%p, bufferLength: %d",
		buffer, (ULONG)bufferLength);

	DumpAsHex(">> BTH", buffer, (ULONG)bufferLength);
#endif

	/*
	* When connected via Bluetooth the Sixaxis occasionally sends
	* a report with the second byte 0xff and the rest zeroed.
	*
	* This report does not reflect the actual state of the
	* controller must be ignored to avoid generating false input
	* events.
	*/
	if (buffer[2] == 0xFF)
	{
		goto resendRequest;
	}

	inputBuffer = &buffer[1];

#pragma region HID Input Report (ID 01) processing

	switch (pDeviceContext->Configuration.HidDeviceMode)
	{
	case DsHidMiniDeviceModeMulti:
		DS3_RAW_TO_SPLIT_HID_INPUT_REPORT_01(
			inputBuffer,
			moduleContext->InputReport,
			pDeviceContext->Configuration.MuteDigitalPressureButtons
		);
		break;
	case DsHidMiniDeviceModeSingle:
		DS3_RAW_TO_SINGLE_HID_INPUT_REPORT(
			inputBuffer,
			moduleContext->InputReport,
			pDeviceContext->Configuration.MuteDigitalPressureButtons
		);
		break;
	default:
		break;
	}

	//
	// Notify new Input Report is available
	// 
	status = DMF_VirtualHidMini_InputReportGenerate(
		moduleContext->DmfModuleVirtualHidMini,
		DsHidMini_RetrieveNextInputReport
	);
	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DSHIDMINIDRV,
			"DMF_VirtualHidMini_InputReportGenerate failed with status %!STATUS!", status);
	}

#pragma endregion

#pragma region HID Input Report (ID 02) processing

	if (pDeviceContext->Configuration.HidDeviceMode == DsHidMiniDeviceModeMulti)
	{
		DS3_RAW_TO_SPLIT_HID_INPUT_REPORT_02(
			inputBuffer,
			moduleContext->InputReport
		);

		//
		// Notify new Input Report is available
		// 
		status = DMF_VirtualHidMini_InputReportGenerate(
			moduleContext->DmfModuleVirtualHidMini,
			DsHidMini_RetrieveNextInputReport
		);
		if (!NT_SUCCESS(status))
		{
			TraceEvents(TRACE_LEVEL_ERROR, TRACE_DSHIDMINIDRV,
				"DMF_VirtualHidMini_InputReportGenerate failed with status %!STATUS!", status);
		}
	}

#pragma endregion

#pragma region HID Input Report (SIXAXIS compatible) processing

	if (pDeviceContext->Configuration.HidDeviceMode == DsHidMiniDeviceModeSixaxisCompatible)
	{
		DS3_RAW_TO_SIXAXIS_HID_INPUT_REPORT(
			inputBuffer,
			moduleContext->InputReport
		);

		//
		// Notify new Input Report is available
		// 
		status = DMF_VirtualHidMini_InputReportGenerate(
			moduleContext->DmfModuleVirtualHidMini,
			DsHidMini_RetrieveNextInputReport
		);
		if (!NT_SUCCESS(status))
		{
			TraceEvents(TRACE_LEVEL_ERROR, TRACE_DSHIDMINIDRV,
				"DMF_VirtualHidMini_InputReportGenerate failed with status %!STATUS!", status);
		}
	}

#pragma endregion

	resendRequest:

	WDF_REQUEST_REUSE_PARAMS_INIT(
		&params,
		WDF_REQUEST_REUSE_NO_FLAGS,
		STATUS_SUCCESS
	);
	status = WdfRequestReuse(Request, &params);
	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR,
			TRACE_DSHIDMINIDRV,
			"WdfRequestReuse failed with status %!STATUS!",
			status
		);
		return;
	}

	status = WdfIoTargetFormatRequestForIoctl(
		pDeviceContext->Connection.Bth.BthIoTarget,
		pDeviceContext->Connection.Bth.HidInterruptReadRequest,
		IOCTL_BTHPS3_HID_INTERRUPT_READ,
		NULL,
		NULL,
		pDeviceContext->Connection.Bth.HidInterruptReadMemory,
		NULL
	);
	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR,
			TRACE_DSHIDMINIDRV,
			"WdfIoTargetFormatRequestForIoctl failed with status %!STATUS!",
			status
		);
	}

	WdfRequestSetCompletionRoutine(
		pDeviceContext->Connection.Bth.HidInterruptReadRequest,
		DsBth_HidInterruptReadRequestCompletionRoutine,
		pDeviceContext
	);

	if (WdfRequestSend(
		pDeviceContext->Connection.Bth.HidInterruptReadRequest,
		pDeviceContext->Connection.Bth.BthIoTarget,
		WDF_NO_SEND_OPTIONS
	) == FALSE) {
		status = WdfRequestGetStatus(pDeviceContext->Connection.Bth.HidInterruptReadRequest);
	}
	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR,
			TRACE_DSHIDMINIDRV,
			"WdfRequestSend failed with status %!STATUS!",
			status
		);
	}

	TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DSHIDMINIDRV, "%!FUNC! Exit");
}

VOID DumpAsHex(PCSTR Prefix, PVOID Buffer, ULONG BufferLength)
{
#ifdef DBG
	PSTR   dumpBuffer;
	size_t  dumpBufferLength;
	ULONG   i;

	dumpBufferLength = ((BufferLength * sizeof(CHAR)) * 2) + 1;
	dumpBuffer = malloc(dumpBufferLength);
	if (dumpBuffer)
	{

		RtlZeroMemory(dumpBuffer, dumpBufferLength);

		for (i = 0; i < BufferLength; i++)
		{
			sprintf(&dumpBuffer[i * 2], "%02X", ((PUCHAR)Buffer)[i]);
		}

		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DSHIDMINIDRV,
			"%s - Buffer length: %04d, buffer content: %s",
			Prefix,
			BufferLength,
			dumpBuffer
		);

		free(dumpBuffer);
	}
#else
	UNREFERENCED_PARAMETER(Prefix);
	UNREFERENCED_PARAMETER(Buffer);
	UNREFERENCED_PARAMETER(BufferLength);
#endif
}
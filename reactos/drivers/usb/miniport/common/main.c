/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS USB miniport driver (Cromwell type)
 * FILE:            drivers/usb/miniport/common/main.c
 * PURPOSE:         Driver entry
 *
 * PROGRAMMERS:     Aleksey Bragin (aleksey@reactos.com)
 *                  Herv� Poussineau (hpoussin@reactos.org),
 *
 * Some parts of code are inspired (or even just copied) from
 * ReactOS Videoport driver (drivers/video/videoprt)
 */

#define NDEBUG
#include <debug.h>

#define INITGUID
#include "usbcommon.h"

// data for embedded drivers
CONNECT_DATA KbdClassInformation;
CONNECT_DATA MouseClassInformation;
PDEVICE_OBJECT KeyboardFdo = NULL;
PDEVICE_OBJECT MouseFdo = NULL;

static NTSTATUS
CreateRootHubPdo(
	IN PDRIVER_OBJECT DriverObject,
	IN PDEVICE_OBJECT Fdo,
	OUT PDEVICE_OBJECT* pPdo)
{
	PDEVICE_OBJECT Pdo;
	PUSBMP_DEVICE_EXTENSION DeviceExtension;
	NTSTATUS Status;
	
	DPRINT("USBMP: CreateRootHubPdo()\n");
	
	Status = IoCreateDevice(
		DriverObject,
		sizeof(USBMP_DEVICE_EXTENSION),
		NULL, /* DeviceName */
		FILE_DEVICE_BUS_EXTENDER,
		FILE_DEVICE_SECURE_OPEN | FILE_AUTOGENERATED_DEVICE_NAME,
		FALSE,
		&Pdo);
	if (!NT_SUCCESS(Status))
	{
		DPRINT("USBMP: IoCreateDevice() call failed with status 0x%08x\n", Status);
		return Status;
	}
	
	Pdo->Flags |= DO_BUS_ENUMERATED_DEVICE;
	Pdo->Flags |= DO_POWER_PAGABLE;
	
	// zerofill device extension
	DeviceExtension = (PUSBMP_DEVICE_EXTENSION)Pdo->DeviceExtension;
	RtlZeroMemory(DeviceExtension, sizeof(USBMP_DEVICE_EXTENSION));
	
	DeviceExtension->IsFDO = FALSE;
	DeviceExtension->FunctionalDeviceObject = Fdo;
	
	Pdo->Flags &= ~DO_DEVICE_INITIALIZING;
	
	*pPdo = Pdo;
	return STATUS_SUCCESS;
}

static NTSTATUS
AddRegistryEntry(
	IN PCWSTR PortTypeName,
	IN PUNICODE_STRING DeviceName,
	IN PCWSTR RegistryPath)
{
	UNICODE_STRING PathU = RTL_CONSTANT_STRING(L"\\REGISTRY\\MACHINE\\HARDWARE\\DEVICEMAP");
	OBJECT_ATTRIBUTES ObjectAttributes;
	HANDLE hDeviceMapKey = (HANDLE)-1;
	HANDLE hPortKey = (HANDLE)-1;
	UNICODE_STRING PortTypeNameU;
	NTSTATUS Status;

	InitializeObjectAttributes(&ObjectAttributes, &PathU, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
	Status = ZwOpenKey(&hDeviceMapKey, 0, &ObjectAttributes);
	if (!NT_SUCCESS(Status))
	{
		DPRINT("ZwOpenKey() failed with status 0x%08lx\n", Status);
		goto cleanup;
	}

	RtlInitUnicodeString(&PortTypeNameU, PortTypeName);
	InitializeObjectAttributes(&ObjectAttributes, &PortTypeNameU, OBJ_KERNEL_HANDLE, hDeviceMapKey, NULL);
	Status = ZwCreateKey(&hPortKey, KEY_SET_VALUE, &ObjectAttributes, 0, NULL, REG_OPTION_VOLATILE, NULL);
	if (!NT_SUCCESS(Status))
	{
		DPRINT("ZwCreateKey() failed with status 0x%08lx\n", Status);
		goto cleanup;
	}

	Status = ZwSetValueKey(hPortKey, DeviceName, 0, REG_SZ, (PVOID)RegistryPath, wcslen(RegistryPath) * sizeof(WCHAR) + sizeof(UNICODE_NULL));
	if (!NT_SUCCESS(Status))
	{
		DPRINT("ZwSetValueKey() failed with status 0x%08lx\n", Status);
		goto cleanup;
	}

	Status = STATUS_SUCCESS;

cleanup:
	if (hDeviceMapKey != (HANDLE)-1)
		ZwClose(hDeviceMapKey);
	if (hPortKey != (HANDLE)-1)
		ZwClose(hPortKey);
	return Status;
}

static NTSTATUS
AddDevice_Keyboard(
	IN PDRIVER_OBJECT DriverObject,
	IN PDEVICE_OBJECT Pdo)
{
	UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(L"\\Device\\KeyboardPortUSB");
	PDEVICE_OBJECT Fdo;
	NTSTATUS Status;

	Status = AddRegistryEntry(L"KeyboardPort", &DeviceName, L"REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Services\\usbport");
	if (!NT_SUCCESS(Status))
	{
		DPRINT1("USBMP: AddRegistryEntry() for usb keyboard driver failed with status 0x%08lx\n", Status);
		return Status;
	}

	Status = IoCreateDevice(DriverObject,
		8, // debug
		&DeviceName,
		FILE_DEVICE_KEYBOARD,
		FILE_DEVICE_SECURE_OPEN,
		TRUE,
		&Fdo);

	if (!NT_SUCCESS(Status))
	{
		DPRINT1("USBMP: IoCreateDevice() for usb keyboard driver failed with status 0x%08lx\n", Status);
		return Status;
	}
	KeyboardFdo = Fdo;
	Fdo->Flags &= ~DO_DEVICE_INITIALIZING;
	DPRINT("USBMP: Created keyboard Fdo: %p\n", Fdo);

	return STATUS_SUCCESS;
}

static NTSTATUS
AddDevice_Mouse(
	IN PDRIVER_OBJECT DriverObject,
	IN PDEVICE_OBJECT Pdo)
{
	UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(L"\\Device\\PointerPortUSB");
	PDEVICE_OBJECT Fdo;
	NTSTATUS Status;

	Status = AddRegistryEntry(L"PointerPort", &DeviceName, L"REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Services\\usbport");
	if (!NT_SUCCESS(Status))
	{
		DPRINT1("USBMP: AddRegistryEntry() for usb mouse driver failed with status 0x%08lx\n", Status);
		return Status;
	}

	Status = IoCreateDevice(DriverObject,
		8, // debug
		&DeviceName,
		FILE_DEVICE_MOUSE,
		FILE_DEVICE_SECURE_OPEN,
		TRUE,
		&Fdo);

	if (!NT_SUCCESS(Status))
	{
		DPRINT1("USBMP: IoCreateDevice() for usb mouse driver failed with status 0x%08lx\n", Status);
		return Status;
	}
	MouseFdo = Fdo;
	Fdo->Flags &= ~DO_DEVICE_INITIALIZING;
	DPRINT("USBMP: Created mouse Fdo: %p\n", Fdo);

	return STATUS_SUCCESS;
}

NTSTATUS STDCALL
AddDevice(
	IN PDRIVER_OBJECT DriverObject,
	IN PDEVICE_OBJECT pdo)
{
	PDEVICE_OBJECT fdo;
	NTSTATUS Status;
	WCHAR DeviceBuffer[20];
	WCHAR LinkDeviceBuffer[20];
	UNICODE_STRING DeviceName;
	UNICODE_STRING LinkDeviceName;
	PUSBMP_DRIVER_EXTENSION DriverExtension;
	PUSBMP_DEVICE_EXTENSION DeviceExtension;
	ULONG DeviceNumber;

	// Allocate driver extension now
	DriverExtension = IoGetDriverObjectExtension(DriverObject, DriverObject);
	if (DriverExtension == NULL)
	{
		Status = IoAllocateDriverObjectExtension(
					DriverObject,
					DriverObject,
					sizeof(USBMP_DRIVER_EXTENSION),
					(PVOID *)&DriverExtension);

		if (!NT_SUCCESS(Status))
		{
			DPRINT("USBMP: Allocating DriverObjectExtension failed.\n");
			return Status;
		}
	}

	// Create a unicode device name
	DeviceNumber = 0; //TODO: Allocate new device number every time
	swprintf(DeviceBuffer, L"\\Device\\USBFDO-%lu", DeviceNumber);
	RtlInitUnicodeString(&DeviceName, DeviceBuffer);

	Status = IoCreateDevice(DriverObject,
				sizeof(USBMP_DEVICE_EXTENSION),
				&DeviceName,
				FILE_DEVICE_BUS_EXTENDER,
				0,
				FALSE,
				&fdo);

	if (!NT_SUCCESS(Status))
	{
		DPRINT("USBMP: IoCreateDevice call failed with status 0x%08lx\n", Status);
		return Status;
	}

	// zerofill device extension
	DeviceExtension = (PUSBMP_DEVICE_EXTENSION)fdo->DeviceExtension;
	RtlZeroMemory(DeviceExtension, sizeof(USBMP_DEVICE_EXTENSION));
	
	/* Create root hub Pdo */
	Status = CreateRootHubPdo(DriverObject, fdo, &DeviceExtension->RootHubPdo);
	if (!NT_SUCCESS(Status))
	{
		DPRINT("USBMP: CreateRootHubPdo() failed with status 0x%08lx\n", Status);
		IoDeleteDevice(fdo);
		return Status;
	}

	/* Register device interface for controller */
	Status = IoRegisterDeviceInterface(
		pdo,
		&GUID_DEVINTERFACE_USB_HOST_CONTROLLER,
		NULL,
		&DeviceExtension->HcdInterfaceName);
	if (!NT_SUCCESS(Status))
	{
		DPRINT("USBMP: IoRegisterDeviceInterface() failed with status 0x%08lx\n", Status);
		IoDeleteDevice(DeviceExtension->RootHubPdo);
		IoDeleteDevice(fdo);
		return Status;
	}

	DeviceExtension->NextDeviceObject = IoAttachDeviceToDeviceStack(fdo, pdo);

	// Initialize device extension
	DeviceExtension->IsFDO = TRUE;
	DeviceExtension->DeviceNumber = DeviceNumber;
	DeviceExtension->PhysicalDeviceObject = pdo;
	DeviceExtension->FunctionalDeviceObject = fdo;
	DeviceExtension->DriverExtension = DriverExtension;

	fdo->Flags &= ~DO_DEVICE_INITIALIZING;
	
	/* FIXME: do a loop to find an available number */
	swprintf(LinkDeviceBuffer, L"\\??\\HCD%lu", 0);

	RtlInitUnicodeString(&LinkDeviceName, LinkDeviceBuffer);

	Status = IoCreateSymbolicLink(&LinkDeviceName, &DeviceName);

	if (NT_SUCCESS(Status))
		Status = AddDevice_Keyboard(DriverObject, pdo);
	if (NT_SUCCESS(Status))
		Status = AddDevice_Mouse(DriverObject, pdo);

	if (!NT_SUCCESS(Status))
	{
		DPRINT("USBMP: IoCreateSymbolicLink() call failed with status 0x%08x\n", Status);
		IoDeleteDevice(DeviceExtension->RootHubPdo);
		IoDeleteDevice(fdo);
		return Status;
	}
	

	return STATUS_SUCCESS;
}

NTSTATUS STDCALL
IrpStub(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp)
{
	NTSTATUS Status = STATUS_NOT_SUPPORTED;

	if (((PUSBMP_DEVICE_EXTENSION)DeviceObject->DeviceExtension)->IsFDO)
	{
		DPRINT1("USBMP: FDO stub for major function 0x%lx\n",
			IoGetCurrentIrpStackLocation(Irp)->MajorFunction);
#ifndef NDEBUG
		DbgBreakPoint();
#endif
		return ForwardIrpAndForget(DeviceObject, Irp);
	}
	else
	{
		/* We can't forward request to the lower driver, because
		 * we are a Pdo, so we don't have lower driver...
		 */
		DPRINT1("USBMP: PDO stub for major function 0x%lx\n",
			IoGetCurrentIrpStackLocation(Irp)->MajorFunction);
#ifndef NDEBUG
		DbgBreakPoint();
#endif
	}

	Status = Irp->IoStatus.Status;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return Status;
}

static NTSTATUS STDCALL
DispatchCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	if (((PUSBMP_DEVICE_EXTENSION)DeviceObject->DeviceExtension)->IsFDO)
		return UsbMpCreate(DeviceObject, Irp);
	else
		return IrpStub(DeviceObject, Irp);
}

static NTSTATUS STDCALL
DispatchClose(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	if (((PUSBMP_DEVICE_EXTENSION)DeviceObject->DeviceExtension)->IsFDO)
		return UsbMpClose(DeviceObject, Irp);
	else
		return IrpStub(DeviceObject, Irp);
}

static NTSTATUS STDCALL
DispatchCleanup(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	if (((PUSBMP_DEVICE_EXTENSION)DeviceObject->DeviceExtension)->IsFDO)
		return UsbMpCleanup(DeviceObject, Irp);
	else
		return IrpStub(DeviceObject, Irp);
}

static NTSTATUS STDCALL
DispatchDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	if (((PUSBMP_DEVICE_EXTENSION)DeviceObject->DeviceExtension)->IsFDO)
		return UsbMpDeviceControlFdo(DeviceObject, Irp);
	else
		return UsbMpDeviceControlPdo(DeviceObject, Irp);
}

static NTSTATUS STDCALL
DispatchInternalDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	if (((PUSBMP_DEVICE_EXTENSION)DeviceObject->DeviceExtension)->IsFDO)
		return UsbMpInternalDeviceControlFdo(DeviceObject, Irp);
	else
		return IrpStub(DeviceObject, Irp);
}

static NTSTATUS STDCALL
DispatchPnp(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	if (((PUSBMP_DEVICE_EXTENSION)DeviceObject->DeviceExtension)->IsFDO)
		return UsbMpPnpFdo(DeviceObject, Irp);
	else
		return UsbMpPnpPdo(DeviceObject, Irp);
}

static NTSTATUS STDCALL 
DispatchPower(PDEVICE_OBJECT fido, PIRP Irp)
{
	DPRINT1("USBMP: IRP_MJ_POWER unimplemented\n");
	Irp->IoStatus.Information = 0;
	Irp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

/*
 * Standard DriverEntry method.
 */
NTSTATUS STDCALL
DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING RegPath)
{
	USBPORT_INTERFACE UsbPortInterface;
	ULONG i;

	DriverObject->DriverUnload = DriverUnload;
	DriverObject->DriverExtension->AddDevice = AddDevice;

	for (i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++)
		DriverObject->MajorFunction[i] = IrpStub;

	DriverObject->MajorFunction[IRP_MJ_CREATE] = DispatchCreate;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = DispatchClose;
	DriverObject->MajorFunction[IRP_MJ_CLEANUP] = DispatchCleanup;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDeviceControl;
	DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = DispatchInternalDeviceControl;
	DriverObject->MajorFunction[IRP_MJ_PNP] = DispatchPnp;
	DriverObject->MajorFunction[IRP_MJ_POWER] = DispatchPower;

	// Register in usbcore.sys
	UsbPortInterface.KbdConnectData = &KbdClassInformation;
	UsbPortInterface.MouseConnectData = &MouseClassInformation;
	
	KbdClassInformation.ClassService = NULL;
	KbdClassInformation.ClassDeviceObject = NULL;
	MouseClassInformation.ClassService = NULL;
	MouseClassInformation.ClassDeviceObject = NULL;
	
	RegisterPortDriver(DriverObject, &UsbPortInterface);

	return STATUS_SUCCESS;
}

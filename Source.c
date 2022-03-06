#include "ntddk.h"
#include "ntddmou.h"

#define MAX_VALUE 65535

ULONG IrpCount = 0;

extern POBJECT_TYPE* IoDriverObjectType;

ObReferenceObjectByName(
	__in PUNICODE_STRING ObjectName,
	__in ULONG Attributes,
	__in_opt PACCESS_STATE AccessState,
	__in_opt ACCESS_MASK DesiredAccess,
	__in POBJECT_TYPE ObjectType,
	__in KPROCESSOR_MODE AccessMode,
	__inout_opt PVOID ParseContext,
	__out PVOID* Object
);

/*
typedef struct _MOUSE_INPUT_DATA {
	USHORT UnitId;
	USHORT Flags;
	union {
		ULONG Buttons;
		struct {
			USHORT ButtonFlags;
			USHORT ButtonData;
		};
	};
	ULONG  RawButtons;
	LONG   LastX;
	LONG   LastY;
	ULONG  ExtraInformation;
} MOUSE_INPUT_DATA, * PMOUSE_INPUT_DATA;
*/

typedef struct {
	PDEVICE_OBJECT attachedDEvice;
	USHORT Flag;
	USHORT IsInversion;
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;


VOID PrintBinary(USHORT Num) {
	CHAR Binary[10] = { '\0' };
	for (int i = 0; i < 8; i++) {
		Binary[i] = (Num & 0x80) ? '1' : '0';
		Num <<= 1;
	}
	KdPrint(("%s", Binary));
}

VOID Unload(PDRIVER_OBJECT DriverObject) {
	PDEVICE_OBJECT DeviceObject = DriverObject->DeviceObject;
	LARGE_INTEGER delay = { 0 };
	delay.QuadPart = -10 * 1000 * 1000;
	while (DeviceObject) {
		IoDetachDevice(((PDEVICE_EXTENSION)DeviceObject->DeviceExtension)->attachedDEvice);
		DeviceObject = DeviceObject->NextDevice;
	}
	while (IrpCount) {
		KeDelayExecutionThread(KernelMode, FALSE, &delay);
	}
	DeviceObject = DriverObject->DeviceObject;
	while (DeviceObject) {
		IoDeleteDevice(DeviceObject);
		DeviceObject = DeviceObject->NextDevice;
	}
	KdPrint(("driver unload\n"));
}

NTSTATUS ReadComplete(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context) {

	PDEVICE_EXTENSION myDeviceExtenson = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;
	PMOUSE_INPUT_DATA MouseData = (PMOUSE_INPUT_DATA)Irp->AssociatedIrp.SystemBuffer;
	int numOfStructs = Irp->IoStatus.Information / sizeof(MOUSE_INPUT_DATA);
	if (Irp->IoStatus.Status == STATUS_SUCCESS) {
		switch (MouseData->ButtonFlags) {
		case MOUSE_LEFT_BUTTON_DOWN:
			if (myDeviceExtenson->Flag == (MOUSE_RIGHT_BUTTON_DOWN | MOUSE_LEFT_BUTTON_DOWN)) {
				myDeviceExtenson->IsInversion = !myDeviceExtenson->IsInversion;
				myDeviceExtenson->Flag = 0;
			}
			else {
				myDeviceExtenson->Flag |= MOUSE_LEFT_BUTTON_DOWN;
			}
			break;
		case MOUSE_RIGHT_BUTTON_DOWN:
			if (myDeviceExtenson->Flag == MOUSE_LEFT_BUTTON_DOWN) {
				myDeviceExtenson->Flag |= MOUSE_RIGHT_BUTTON_DOWN;
			}
			else {
				myDeviceExtenson->Flag = 0;
			}
			break;
		default:
			break;
		}
		KdPrint(("Button: %d", MouseData->ButtonFlags));
		KdPrint(("IsInversion: %d", myDeviceExtenson->IsInversion));
		KdPrint(("Flag: "));
		PrintBinary(myDeviceExtenson->Flag);
		if (myDeviceExtenson->IsInversion) {
			if ((MouseData->Flags & MOUSE_MOVE_ABSOLUTE) != 0) {
				MouseData->LastY = MAX_VALUE - MouseData->LastY;
			}
			else {
				MouseData->LastY = -MouseData->LastY;
			}
		}
	}

	if (Irp->PendingReturned) {
		IoMarkIrpPending(Irp);
	}

	IrpCount--;
	return Irp->IoStatus.Status;
}

NTSTATUS DispatchPass(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
	IoCopyCurrentIrpStackLocationToNext(Irp);
	return IoCallDriver(((PDEVICE_EXTENSION)DeviceObject->DeviceExtension)->attachedDEvice, Irp);
}

NTSTATUS DispatchRead(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
	IoCopyCurrentIrpStackLocationToNext(Irp);
	IoSetCompletionRoutine(Irp, ReadComplete, NULL, TRUE, TRUE, TRUE);
	IrpCount++;
	return IoCallDriver(((PDEVICE_EXTENSION)DeviceObject->DeviceExtension)->attachedDEvice, Irp);
}

NTSTATUS MyAttachDevice(PDRIVER_OBJECT DriverObject) {
	NTSTATUS status = STATUS_SUCCESS;
	UNICODE_STRING MouseClassName = RTL_CONSTANT_STRING(L"\\Driver\\Mouclass");
	PDRIVER_OBJECT MouseClassDriver = NULL;
	PDEVICE_OBJECT CurrentDevice = NULL;
	PDEVICE_OBJECT myDeviceObject = NULL;

	status = ObReferenceObjectByName(&MouseClassName, OBJ_CASE_INSENSITIVE, NULL, 0, *IoDriverObjectType, KernelMode, NULL, (PVOID*)&MouseClassDriver);
	if (!NT_SUCCESS(status)) {
		KdPrint(("ObReferenceObjectByName failed\n"));
		return status;
	}
	ObDereferenceObject(MouseClassDriver);
	CurrentDevice = MouseClassDriver->DeviceObject;
	while (CurrentDevice) {
		status = IoCreateDevice(DriverObject, sizeof(DEVICE_EXTENSION), NULL, FILE_DEVICE_MOUSE, 0, FALSE, &myDeviceObject);
		if (!NT_SUCCESS(status)) {
			KdPrint(("IoCreateDevice failed"));
			return status;
		}
		RtlZeroMemory(myDeviceObject->DeviceExtension, sizeof(DEVICE_EXTENSION));
		status = IoAttachDeviceToDeviceStackSafe(myDeviceObject, MouseClassDriver->DeviceObject, &((PDEVICE_EXTENSION)myDeviceObject->DeviceExtension)->attachedDEvice);
		if (!NT_SUCCESS(status)) {
			KdPrint(("IoAttachDeviceToDeviceStack failed\n"));
			IoDeleteDevice(myDeviceObject);
			return status;
		}
		myDeviceObject->Flags |= (((PDEVICE_EXTENSION)myDeviceObject->DeviceExtension)->attachedDEvice)->Flags & (DO_BUFFERED_IO | \
			DO_DIRECT_IO);
		myDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
		myDeviceObject->Flags |= DO_POWER_PAGABLE;
		myDeviceObject->DeviceType = (((PDEVICE_EXTENSION)myDeviceObject->DeviceExtension)->attachedDEvice)->DeviceType;
		CurrentDevice = CurrentDevice->NextDevice;
	}
	
	return STATUS_SUCCESS;
}


NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath) {

	NTSTATUS status = STATUS_SUCCESS;

	DriverObject->DriverUnload = Unload;

	for (int i = 0; i < ARRAYSIZE(DriverObject->MajorFunction); i++) {
		DriverObject->MajorFunction[i] = DispatchPass;
	}
	DriverObject->MajorFunction[IRP_MJ_READ] = DispatchRead;

	KdPrint(("driver load\n"));

	status = MyAttachDevice(DriverObject);
	if (!NT_SUCCESS(status)) {
		KdPrint(("attaching failed\n"));
	}
	else {
		KdPrint(("attaching succeeds\n"));
		KdPrint(("right: "));
		PrintBinary(MOUSE_RIGHT_BUTTON_DOWN);
		KdPrint(("left: "));
		PrintBinary(MOUSE_LEFT_BUTTON_DOWN);
	}
	return status;
}

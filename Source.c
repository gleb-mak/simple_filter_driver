#include "ntddk.h"
#include "ntddmou.h"

#define MAX_VALUE 65535

PDEVICE_OBJECT myKbdDevice = NULL;
ULONG IrpCount = 0;

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
	PDEVICE_OBJECT attachedKbdDEvice;
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
	IoDetachDevice(((PDEVICE_EXTENSION)DeviceObject->DeviceExtension)->attachedKbdDEvice);
	while (IrpCount) {
		KeDelayExecutionThread(KernelMode, FALSE, &delay);
	}
	IoDeleteDevice(DeviceObject);
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
	return IoCallDriver(((PDEVICE_EXTENSION)DeviceObject->DeviceExtension)->attachedKbdDEvice, Irp);
}

NTSTATUS DispatchRead(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
	IoCopyCurrentIrpStackLocationToNext(Irp);
	IoSetCompletionRoutine(Irp, ReadComplete, NULL, TRUE, TRUE, TRUE);
	IrpCount++;
	return IoCallDriver(((PDEVICE_EXTENSION)DeviceObject->DeviceExtension)->attachedKbdDEvice, Irp);
}

NTSTATUS MyAttachDevice(PDRIVER_OBJECT DriverObject) {
	NTSTATUS status = STATUS_SUCCESS;
	UNICODE_STRING TargetDevice = RTL_CONSTANT_STRING(L"\\Device\\PointerClass0");

	status = IoCreateDevice(DriverObject, sizeof(DEVICE_EXTENSION), NULL, FILE_DEVICE_KEYBOARD, 0, FALSE, &myKbdDevice);
	if (!NT_SUCCESS(status)) {
		KdPrint(("IoCreateDevice failed"));
		return status;
	}
	RtlZeroMemory(myKbdDevice->DeviceExtension, sizeof(DEVICE_EXTENSION));
	status = IoAttachDevice(myKbdDevice, &TargetDevice, &((PDEVICE_EXTENSION)myKbdDevice->DeviceExtension)->attachedKbdDEvice);
	if (!NT_SUCCESS(status)) {
		KdPrint(("IoAttachDeviceToDeviceStack failed\n"));
		IoDeleteDevice(myKbdDevice);
		return status;
	}
	myKbdDevice->Flags |= (((PDEVICE_EXTENSION)myKbdDevice->DeviceExtension)->attachedKbdDEvice)->Flags & (DO_BUFFERED_IO | \
		DO_DIRECT_IO);
	myKbdDevice->Flags &= ~DO_DEVICE_INITIALIZING;
	myKbdDevice->Flags |= DO_POWER_PAGABLE;
	myKbdDevice->DeviceType = (((PDEVICE_EXTENSION)myKbdDevice->DeviceExtension)->attachedKbdDEvice)->DeviceType;
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
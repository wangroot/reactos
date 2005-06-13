/* $Id:
 *
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Serial enumerator driver
 * FILE:            drivers/bus/serenum/detect.c
 * PURPOSE:         Detection of serial devices
 *
 * PROGRAMMERS:     Jason Filby (jasonfilby@yahoo.com)
 *                  Filip Navara (xnavara@volny.cz)
 *                  Herv� Poussineau (hpoussin@reactos.com)
 */

#define NDEBUG
#include "serenum.h"

static NTSTATUS
SerenumDeviceIoControl(
	IN PDEVICE_OBJECT DeviceObject,
	IN ULONG CtlCode,
	IN PVOID InputBuffer OPTIONAL,
	IN ULONG InputBufferSize,
	IN OUT PVOID OutputBuffer OPTIONAL,
	IN OUT PULONG OutputBufferSize)
{
	KEVENT Event;
	PIRP Irp;
	IO_STATUS_BLOCK IoStatus;
	NTSTATUS Status;

	KeInitializeEvent (&Event, NotificationEvent, FALSE);

	Irp = IoBuildDeviceIoControlRequest(CtlCode,
		DeviceObject,
		InputBuffer,
		InputBufferSize,
		OutputBuffer,
		(OutputBufferSize) ? *OutputBufferSize : 0,
		FALSE,
		&Event,
		&IoStatus);
	if (Irp == NULL)
	{
		DPRINT("Serenum: IoBuildDeviceIoControlRequest() failed\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	Status = IoCallDriver(DeviceObject, Irp);

	if (Status == STATUS_PENDING)
	{
		DPRINT("Serenum: Operation pending\n");
		KeWaitForSingleObject(&Event, Suspended, KernelMode, FALSE, NULL);
		Status = IoStatus.Status;
	}

	if (OutputBufferSize)
	{
		*OutputBufferSize = IoStatus.Information;
	}

	return Status;
}

static NTSTATUS
SerenumSendIrp(
	IN PDEVICE_OBJECT DeviceObject,
	IN ULONG MajorFunction)
{
	KEVENT Event;
	PIRP Irp;
	IO_STATUS_BLOCK IoStatus;
	NTSTATUS Status;

	KeInitializeEvent(&Event, NotificationEvent, FALSE);
	
	Irp = IoBuildSynchronousFsdRequest(
		MajorFunction,
		DeviceObject,
		NULL,
		0,
		NULL,
		&Event,
		&IoStatus);
	if (Irp == NULL)
	{
		DPRINT("Serenum: IoBuildSynchronousFsdRequest() failed\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	
	Status = IoCallDriver(DeviceObject, Irp);

	if (Status == STATUS_PENDING)
	{
		DPRINT("Serenum: Operation pending\n");
		KeWaitForSingleObject(&Event, Suspended, KernelMode, FALSE, NULL);
		Status = IoStatus.Status;
	}

	return Status;
}

static NTSTATUS
ReadBytes(
	IN PDEVICE_OBJECT LowerDevice,
	OUT PUCHAR Buffer,
	IN ULONG BufferSize,
	OUT PULONG FilledBytes)
{
	PIRP Irp;
	IO_STATUS_BLOCK ioStatus;
	KEVENT event;
	LARGE_INTEGER zero;
	NTSTATUS Status;

	KeInitializeEvent(&event, NotificationEvent, FALSE);
	zero.QuadPart = 0;
	Irp = IoBuildSynchronousFsdRequest(
		IRP_MJ_READ,
		LowerDevice,
		Buffer, BufferSize,
		&zero,
		&event,
		&ioStatus);
	if (!Irp)
		return FALSE;

	Status = IoCallDriver(LowerDevice, Irp);
	if (Status == STATUS_PENDING)
	{
		KeWaitForSingleObject(&event, Suspended, KernelMode, FALSE, NULL);
		Status = ioStatus.Status;
	}
	DPRINT("Serenum: bytes received: %lu/%lu\n",
		ioStatus.Information, BufferSize);
	*FilledBytes = ioStatus.Information;
	return Status;
}

static NTSTATUS
ReportDetectedDevice(
	IN PDEVICE_OBJECT DeviceObject,
	IN PUNICODE_STRING DeviceDescription,
	IN PUNICODE_STRING DeviceId,
	IN PUNICODE_STRING InstanceId,
	IN PUNICODE_STRING HardwareIds,
	IN PUNICODE_STRING CompatibleIds)
{
	PDEVICE_OBJECT Pdo = NULL;
	PPDO_DEVICE_EXTENSION PdoDeviceExtension = NULL;
	PFDO_DEVICE_EXTENSION FdoDeviceExtension;
	NTSTATUS Status;

	DPRINT("Serenum: ReportDetectedDevice() called with %wZ (%wZ) detected\n", DeviceId, DeviceDescription);

	Status = IoCreateDevice(
		DeviceObject->DriverObject,
		sizeof(PDO_DEVICE_EXTENSION),
		NULL,
		FILE_DEVICE_CONTROLLER,
		FILE_AUTOGENERATED_DEVICE_NAME,
		FALSE,
		&Pdo);
	if (!NT_SUCCESS(Status)) goto ByeBye;

	Pdo->Flags |= DO_BUS_ENUMERATED_DEVICE;
	Pdo->Flags |= DO_POWER_PAGABLE;
	PdoDeviceExtension = (PPDO_DEVICE_EXTENSION)Pdo->DeviceExtension;
	FdoDeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
	RtlZeroMemory(PdoDeviceExtension, sizeof(PDO_DEVICE_EXTENSION));
	PdoDeviceExtension->Common.IsFDO = FALSE;
	Status = SerenumDuplicateUnicodeString(&PdoDeviceExtension->DeviceDescription, DeviceDescription, PagedPool);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	Status = SerenumDuplicateUnicodeString(&PdoDeviceExtension->DeviceId, DeviceId, PagedPool);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	Status = SerenumDuplicateUnicodeString(&PdoDeviceExtension->InstanceId, InstanceId, PagedPool);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	Status = SerenumDuplicateUnicodeString(&PdoDeviceExtension->HardwareIds, HardwareIds, PagedPool);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	Status = SerenumDuplicateUnicodeString(&PdoDeviceExtension->CompatibleIds, CompatibleIds, PagedPool);
	if (!NT_SUCCESS(Status)) goto ByeBye;

	/* Device attached to serial port (Pdo) may delegate work to
	 * serial port stack (Fdo = DeviceObject variable) */
	Pdo->StackSize = DeviceObject->StackSize + 1;

	FdoDeviceExtension->AttachedPdo = Pdo;
	PdoDeviceExtension->AttachedFdo = DeviceObject;

	Pdo->Flags |= DO_BUFFERED_IO;
	Pdo->Flags &= ~DO_DEVICE_INITIALIZING;

	return STATUS_SUCCESS;

ByeBye:
	if (Pdo)
	{
		if (PdoDeviceExtension->DeviceDescription.Buffer)
			RtlFreeUnicodeString(&PdoDeviceExtension->DeviceDescription);
		if (PdoDeviceExtension->DeviceId.Buffer)
			RtlFreeUnicodeString(&PdoDeviceExtension->DeviceId);
		if (PdoDeviceExtension->InstanceId.Buffer)
			RtlFreeUnicodeString(&PdoDeviceExtension->InstanceId);
		if (PdoDeviceExtension->HardwareIds.Buffer)
			RtlFreeUnicodeString(&PdoDeviceExtension->HardwareIds);
		if (PdoDeviceExtension->CompatibleIds.Buffer)
			RtlFreeUnicodeString(&PdoDeviceExtension->CompatibleIds);
		IoDeleteDevice(Pdo);
	}
	return Status;
}

static BOOLEAN
SerenumIsValidPnpIdString(
	IN PUCHAR Buffer,
	IN ULONG BufferLength)
{
	/* FIXME: SerenumIsValidPnpIdString not implemented */
	DPRINT1("Serenum: SerenumIsValidPnpIdString() unimplemented\n");
	return STATUS_SUCCESS;
}

static NTSTATUS
ReportDetectedPnpDevice(
	IN PUCHAR Buffer,
	IN ULONG BufferLength)
{
	ULONG i;
	/* FIXME: ReportDetectedPnpDevice not implemented */
	DPRINT1("Serenum: ReportDetectedPnpDevice() unimplemented\n");
	DPRINT1("");
	for (i = 0; i < BufferLength; i++)
		DbgPrint("%c", Buffer[i]);
	DbgPrint("\n");
	/* Call ReportDetectedDevice */
	return STATUS_SUCCESS;
}

#define BEGIN_ID '('
#define END_ID ')'

static NTSTATUS
SerenumWait(ULONG milliseconds)
{
	KTIMER Timer;
	LARGE_INTEGER DueTime;

	DueTime.QuadPart = milliseconds * -10;
	KeInitializeTimer(&Timer);
	KeSetTimer(&Timer, DueTime, NULL);
	return KeWaitForSingleObject(&Timer, Executive, KernelMode, FALSE, NULL);
}

NTSTATUS
SerenumDetectPnpDevice(
	IN PDEVICE_OBJECT DeviceObject,
	IN PDEVICE_OBJECT LowerDevice)
{
	UCHAR Buffer[256];
	ULONG BaudRate;
	ULONG TotalBytesReceived = 0;
	ULONG Size;
	ULONG Msr, Purge;
	ULONG i;
	BOOLEAN BufferContainsBeginId = FALSE;
	BOOLEAN BufferContainsEndId = FALSE;
	SERIAL_LINE_CONTROL Lcr;
	SERIAL_TIMEOUTS Timeouts;
	SERIALPERF_STATS PerfStats;
	NTSTATUS Status;
	
	/* Open port */
	Status = SerenumSendIrp(LowerDevice, IRP_MJ_CREATE);
	if (!NT_SUCCESS(Status)) goto ByeBye;

	/* 1. COM port initialization, check for device enumerate */
	CHECKPOINT;
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_CLR_DTR,
		NULL, 0, NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_CLR_RTS,
		NULL, 0, NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	SerenumWait(200);
	Size = sizeof(Msr);
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_GET_MODEMSTATUS,
		NULL, 0, &Msr, &Size);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	if ((Msr & SR_MSR_DSR) == 0) goto SerenumDisconnectIdle;

	/* 2. COM port setup, 1st phase */
	CHECKPOINT;
	BaudRate = 1200;
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_SET_BAUD_RATE,
		&BaudRate, sizeof(BaudRate), NULL, 0);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	Lcr.WordLength = 7;
	Lcr.Parity = NO_PARITY;
	Lcr.StopBits = STOP_BIT_1;
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_SET_LINE_CONTROL,
		&Lcr, sizeof(Lcr), NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_CLR_DTR,
		NULL, 0, NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_CLR_RTS,
		NULL, 0, NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	SerenumWait(200);
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_SET_DTR,
		NULL, 0, NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	SerenumWait(200);

	/* 3. Wait for response, 1st phase */
	CHECKPOINT;
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_SET_RTS,
		NULL, 0, NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	Timeouts.ReadIntervalTimeout = 0;
	Timeouts.ReadTotalTimeoutMultiplier = 0;
	Timeouts.ReadTotalTimeoutConstant = 200;
	Timeouts.WriteTotalTimeoutMultiplier = Timeouts.WriteTotalTimeoutConstant = 0;
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_SET_TIMEOUTS,
		&Timeouts, sizeof(Timeouts), NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	Status = ReadBytes(LowerDevice, Buffer, sizeof(Buffer), &Size);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	if (Size != 0) goto SerenumCollectPnpComDeviceId;

	/* 4. COM port setup, 2nd phase */
	CHECKPOINT;
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_CLR_DTR,
		NULL, 0, NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_CLR_RTS,
		NULL, 0, NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	Purge = SERIAL_PURGE_RXABORT | SERIAL_PURGE_RXCLEAR;
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_PURGE,
		&Purge, 0, NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	SerenumWait(200);

	/* 5. Wait for response, 2nd phase */
	CHECKPOINT;
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_SET_DTR,
		NULL, 0, NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_SET_RTS,
		NULL, 0, NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	Status = ReadBytes(LowerDevice, Buffer, 1, &TotalBytesReceived);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	if (TotalBytesReceived != 0) goto SerenumCollectPnpComDeviceId;
	Size = sizeof(Msr);
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_GET_MODEMSTATUS,
		NULL, 0, &Msr, &Size);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	if ((Msr & SR_MSR_DSR) == 0) goto SerenumVerifyDisconnect; else goto SerenumConnectIdle;

	/* 6. Collect PnP COM device ID */
SerenumCollectPnpComDeviceId:
	CHECKPOINT;
	Timeouts.ReadIntervalTimeout = 200;
	Timeouts.ReadTotalTimeoutMultiplier = 0;
	Timeouts.ReadTotalTimeoutConstant = 2200;
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_SET_TIMEOUTS,
		&Timeouts, sizeof(Timeouts), NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	Status = ReadBytes(LowerDevice, &Buffer[TotalBytesReceived], sizeof(Buffer) - TotalBytesReceived, &Size);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	TotalBytesReceived += Size;
	Size = sizeof(PerfStats);
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_GET_STATS,
		NULL, 0, &PerfStats, &Size);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	if (PerfStats.FrameErrorCount + PerfStats.ParityErrorCount != 0) goto SerenumConnectIdle;
	for (i = 0; i < TotalBytesReceived; i++)
	{
		if (Buffer[i] == BEGIN_ID) BufferContainsBeginId = TRUE;
		if (Buffer[i] == END_ID) BufferContainsEndId = TRUE;
	}
	if (TotalBytesReceived == 1 || BufferContainsEndId)
	{
		if (SerenumIsValidPnpIdString(Buffer, TotalBytesReceived))
		{
			Status = ReportDetectedPnpDevice(Buffer, TotalBytesReceived);
			goto ByeBye;
		}
		goto SerenumConnectIdle;
	}
	if (!BufferContainsBeginId) goto SerenumConnectIdle;
	if (!BufferContainsEndId) goto SerenumConnectIdle;
	Size = sizeof(Msr);
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_GET_MODEMSTATUS,
		NULL, 0, &Msr, &Size);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	if ((Msr & SR_MSR_DSR) == 0) goto SerenumVerifyDisconnect;

	/* 7. Verify disconnect */
SerenumVerifyDisconnect:
	CHECKPOINT;
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_SET_DTR,
		NULL, 0, NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_CLR_RTS,
		NULL, 0, NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	SerenumWait(5000);
	goto SerenumDisconnectIdle;

	/* 8. Connect idle */
SerenumConnectIdle:
	CHECKPOINT;
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_SET_DTR,
		NULL, 0, NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_CLR_RTS,
		NULL, 0, NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	BaudRate = 300;
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_SET_BAUD_RATE,
		&BaudRate, sizeof(BaudRate), NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	Lcr.WordLength = 7;
	Lcr.Parity = NO_PARITY;
	Lcr.StopBits = STOP_BIT_1;
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_SET_LINE_CONTROL,
		&Lcr, sizeof(Lcr), NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	if (TotalBytesReceived == 0)
		Status = STATUS_DEVICE_NOT_CONNECTED;
	else
		Status = STATUS_SUCCESS;
	goto ByeBye;

	/* 9. Disconnect idle */
SerenumDisconnectIdle:
	CHECKPOINT;
	/* FIXME: report to OS device removal, if it was present */
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_SET_DTR,
		NULL, 0, NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_CLR_RTS,
		NULL, 0, NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	BaudRate = 300;
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_SET_BAUD_RATE,
		&BaudRate, sizeof(BaudRate), NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	Lcr.WordLength = 7;
	Lcr.Parity = NO_PARITY;
	Lcr.StopBits = STOP_BIT_1;
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_SET_LINE_CONTROL,
		&Lcr, sizeof(Lcr), NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	Status = STATUS_DEVICE_NOT_CONNECTED;

ByeBye:
	/* Close port */
	SerenumSendIrp(LowerDevice, IRP_MJ_CLOSE);
	SerenumSendIrp(LowerDevice, IRP_MJ_CLEANUP);
	return Status;
}

NTSTATUS
SerenumDetectLegacyDevice(
	IN PDEVICE_OBJECT DeviceObject,
	IN PDEVICE_OBJECT LowerDevice)
{
	ULONG Fcr, Mcr;
	ULONG BaudRate;
	ULONG Command;
	SERIAL_TIMEOUTS Timeouts;
	SERIAL_LINE_CONTROL LCR;
	ULONG i, Count;
	UCHAR Buffer[16];
	UNICODE_STRING DeviceDescription;
	UNICODE_STRING DeviceId;
	UNICODE_STRING InstanceId;
	UNICODE_STRING HardwareIds;
	UNICODE_STRING CompatibleIds;
	NTSTATUS Status;

	DPRINT("Serenum: SerenumDetectLegacyDevice(DeviceObject %p, LowerDevice %p)\n",
		DeviceObject,
		LowerDevice);

	RtlZeroMemory(Buffer, sizeof(Buffer));
	
	/* Open port */
	Status = SerenumSendIrp(LowerDevice, IRP_MJ_CREATE);
	if (!NT_SUCCESS(Status)) return Status;

	/* Reset UART */
	CHECKPOINT;
	Mcr = 0; /* MCR: DTR/RTS/OUT2 off */
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_SET_MODEM_CONTROL,
		&Mcr, sizeof(Mcr), NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;

	/* Set communications parameters */
	CHECKPOINT;
	/* DLAB off */
	Fcr = 0;
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_SET_FIFO_CONTROL,
		&Fcr, sizeof(Fcr), NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	/* Set serial port speed */
	BaudRate = 1200;
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_SET_BAUD_RATE,
		&BaudRate, sizeof(BaudRate), NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	/* Set LCR */
	LCR.WordLength = 7;
	LCR.Parity = NO_PARITY;
	LCR.StopBits = STOP_BITS_2;
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_SET_LINE_CONTROL,
		&LCR, sizeof(LCR), NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;

	/* Flush receive buffer */
	CHECKPOINT;
	Command = SERIAL_PURGE_RXCLEAR;
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_SET_MODEM_CONTROL,
		&Command, sizeof(Command), NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	/* Wait 100 ms */
	SerenumWait(100);

	/* Enable DTR/RTS */
	CHECKPOINT;
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_SET_DTR,
		NULL, 0, NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_SET_RTS,
		NULL, 0, NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;

	/* Set timeout to 500 microseconds */
	CHECKPOINT;
	Timeouts.ReadIntervalTimeout = 100;
	Timeouts.ReadTotalTimeoutMultiplier = 0;
	Timeouts.ReadTotalTimeoutConstant = 500;
	Timeouts.WriteTotalTimeoutMultiplier = Timeouts.WriteTotalTimeoutConstant = 0;
	Status = SerenumDeviceIoControl(LowerDevice, IOCTL_SERIAL_SET_TIMEOUTS,
		&Timeouts, sizeof(Timeouts), NULL, NULL);
	if (!NT_SUCCESS(Status)) goto ByeBye;

	/* Fill the read buffer */
	CHECKPOINT;
	Status = ReadBytes(LowerDevice, Buffer, sizeof(Buffer)/sizeof(Buffer[0]), &Count);
	if (!NT_SUCCESS(Status)) goto ByeBye;

	RtlInitUnicodeString(&DeviceId, L"Serenum\\Mouse");
	RtlInitUnicodeString(&InstanceId, L"0000"); /* FIXME */
	for (i = 0; i < Count; i++)
	{
		if (Buffer[i] == 'B')
		{
			/* Sign for Microsoft Ballpoint */
			/* Hardware id: *PNP0F09
			 * Compatible id: *PNP0F0F, SERIAL_MOUSE
			 */
			RtlInitUnicodeString(&DeviceDescription, L"Microsoft Ballpoint device");
			SerenumInitMultiSzString(&HardwareIds, "*PNP0F09", NULL);
			SerenumInitMultiSzString(&CompatibleIds, "*PNP0F0F", "SERIAL_MOUSE", NULL);
			Status = ReportDetectedDevice(DeviceObject,
				&DeviceDescription, &DeviceId, &InstanceId, &HardwareIds, &CompatibleIds);
			RtlFreeUnicodeString(&HardwareIds);
			RtlFreeUnicodeString(&CompatibleIds);
			goto ByeBye;
		}
		else if (Buffer[i] == 'M')
		{
			/* Sign for Microsoft Mouse protocol followed by button specifier */
			if (i == sizeof(Buffer) - 1)
			{
				/* Overflow Error */
				Status = STATUS_DEVICE_NOT_CONNECTED;
				goto ByeBye;
			}
			switch (Buffer[i + 1])
			{
				case '3':
					/* Hardware id: *PNP0F08
					 * Compatible id: SERIAL_MOUSE
					 */
					RtlInitUnicodeString(&DeviceDescription, L"Microsoft Mouse with 3-buttons");
					SerenumInitMultiSzString(&HardwareIds, "*PNP0F08", NULL);
					SerenumInitMultiSzString(&CompatibleIds, "SERIAL_MOUSE", NULL);
				default:
					/* Hardware id: *PNP0F01
					 * Compatible id: SERIAL_MOUSE
					 */
					RtlInitUnicodeString(&DeviceDescription, L"Microsoft Mouse with 2-buttons or Microsoft Wheel Mouse");
					SerenumInitMultiSzString(&HardwareIds, "*PNP0F01", NULL);
					SerenumInitMultiSzString(&CompatibleIds, "SERIAL_MOUSE", NULL);
			}
			Status = ReportDetectedDevice(DeviceObject,
				&DeviceDescription, &DeviceId, &InstanceId, &HardwareIds, &CompatibleIds);
			RtlFreeUnicodeString(&HardwareIds);
			RtlFreeUnicodeString(&CompatibleIds);
			goto ByeBye;
		}
	}

	Status = STATUS_DEVICE_NOT_CONNECTED;
	
ByeBye:
	/* Close port */
	SerenumSendIrp(LowerDevice, IRP_MJ_CLOSE);
	SerenumSendIrp(LowerDevice, IRP_MJ_CLEANUP);
	return Status;
}

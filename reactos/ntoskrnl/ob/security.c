/*
* PROJECT:         ReactOS Kernel
* LICENSE:         GPL - See COPYING in the top level directory
* FILE:            ntoskrnl/ob/security.c
* PURPOSE:         SRM Interface of the Object Manager
* PROGRAMMERS:     Alex Ionescu (alex@relsoft.net)
*/

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <internal/debug.h>

/* FUNCTIONS ***************************************************************/

/*++
* @name ObAssignSecurity
* @implemented NT4
*
*     The ObAssignSecurity routine <FILLMEIN>
*
* @param AccessState
*        <FILLMEIN>
*
* @param SecurityDescriptor
*        <FILLMEIN>
*
* @param Object
*        <FILLMEIN>
*
* @param Type
*        <FILLMEIN>
*
* @return STATUS_SUCCESS or appropriate error value.
*
* @remarks None.
*
*--*/
NTSTATUS
NTAPI
ObAssignSecurity(IN PACCESS_STATE AccessState,
                 IN PSECURITY_DESCRIPTOR SecurityDescriptor,
                 IN PVOID Object,
                 IN POBJECT_TYPE Type)
{
    PSECURITY_DESCRIPTOR NewDescriptor;
    NTSTATUS Status;
    PAGED_CODE();

    /* Build the new security descriptor */
    Status = SeAssignSecurity(SecurityDescriptor,
                              AccessState->SecurityDescriptor,
                              &NewDescriptor,
                              (Type == ObDirectoryType),
                              &AccessState->SubjectSecurityContext,
                              &Type->TypeInfo.GenericMapping,
                              PagedPool);
    if (!NT_SUCCESS(Status)) return Status;

    /* Call the security method */
    Status = Type->TypeInfo.SecurityProcedure(Object,
                                              AssignSecurityDescriptor,
                                              0,
                                              NewDescriptor,
                                              NULL,
                                              NULL,
                                              NonPagedPool,
                                              NULL);

    /* Release the new security descriptor */
    SeDeassignSecurity(&NewDescriptor);

    return Status;
}

/*++
* @name ObGetObjectSecurity
* @implemented NT4
*
*     The ObGetObjectSecurity routine <FILLMEIN>
*
* @param Object
*        <FILLMEIN>
*
* @param SecurityDescriptor
*        <FILLMEIN>
*
* @param MemoryAllocated
*        <FILLMEIN>
*
* @return STATUS_SUCCESS or appropriate error value.
*
* @remarks None.
*
*--*/
NTSTATUS
NTAPI
ObGetObjectSecurity(IN PVOID Object,
                    OUT PSECURITY_DESCRIPTOR *SecurityDescriptor,
                    OUT PBOOLEAN MemoryAllocated)
{
    POBJECT_HEADER Header;
    ULONG Length;
    NTSTATUS Status;
    PAGED_CODE();

    Header = OBJECT_TO_OBJECT_HEADER(Object);
    if (Header->Type == NULL) return STATUS_UNSUCCESSFUL;

    if (Header->Type->TypeInfo.SecurityProcedure == NULL)
    {
        ObpReferenceCachedSecurityDescriptor(Header->SecurityDescriptor);
        *SecurityDescriptor = Header->SecurityDescriptor;
        *MemoryAllocated = FALSE;
        return STATUS_SUCCESS;
    }

    /* Get the security descriptor size */
    Length = 0;
    Status = Header->Type->TypeInfo.SecurityProcedure(Object,
                                                      QuerySecurityDescriptor,
                                                      OWNER_SECURITY_INFORMATION |
                                                      GROUP_SECURITY_INFORMATION |
                                                      DACL_SECURITY_INFORMATION |
                                                      SACL_SECURITY_INFORMATION,
                                                      NULL,
                                                      &Length,
                                                      NULL,
                                                      NonPagedPool,
                                                      NULL);
    if (Status != STATUS_BUFFER_TOO_SMALL) return Status;

    /* Allocate security descriptor */
    *SecurityDescriptor = ExAllocatePool(NonPagedPool, Length);
    if (*SecurityDescriptor == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    /* Query security descriptor */
    Status = Header->Type->TypeInfo.SecurityProcedure(Object,
                                                      QuerySecurityDescriptor,
                                                      OWNER_SECURITY_INFORMATION |
                                                      GROUP_SECURITY_INFORMATION |
                                                      DACL_SECURITY_INFORMATION |
                                                      SACL_SECURITY_INFORMATION,
                                                      *SecurityDescriptor,
                                                      &Length,
                                                      NULL,
                                                      NonPagedPool,
                                                      NULL);
    if (!NT_SUCCESS(Status))
    {
        ExFreePool(*SecurityDescriptor);
        return Status;
    }

    *MemoryAllocated = TRUE;

    return STATUS_SUCCESS;
}

/*++
* @name ObReleaseObjectSecurity
* @implemented NT4
*
*     The ObReleaseObjectSecurity routine <FILLMEIN>
*
* @param SecurityDescriptor
*        <FILLMEIN>
*
* @param MemoryAllocated
*        <FILLMEIN>
*
* @return STATUS_SUCCESS or appropriate error value.
*
* @remarks None.
*
*--*/
VOID
NTAPI
ObReleaseObjectSecurity(IN PSECURITY_DESCRIPTOR SecurityDescriptor,
                        IN BOOLEAN MemoryAllocated)
{
    PAGED_CODE();

    if (SecurityDescriptor == NULL) return;

    if (MemoryAllocated)
    {
        ExFreePool(SecurityDescriptor);
    }
    else
    {
        ObpDereferenceCachedSecurityDescriptor(SecurityDescriptor);
    }
}

/*++
* @name NtQuerySecurityObject
* @implemented NT4
*
*     The NtQuerySecurityObject routine <FILLMEIN>
*
* @param Handle
*        <FILLMEIN>
*
* @param SecurityInformation
*        <FILLMEIN>
*
* @param SecurityDescriptor
*        <FILLMEIN>
*
* @param Length
*        <FILLMEIN>
*
* @param ResultLength
*        <FILLMEIN>
*
* @return STATUS_SUCCESS or appropriate error value.
*
* @remarks None.
*
*--*/
NTSTATUS
NTAPI
NtQuerySecurityObject(IN HANDLE Handle,
                      IN SECURITY_INFORMATION SecurityInformation,
                      OUT PSECURITY_DESCRIPTOR SecurityDescriptor,
                      IN ULONG Length,
                      OUT PULONG ResultLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PVOID Object;
    POBJECT_HEADER Header;
    ACCESS_MASK DesiredAccess = (ACCESS_MASK)0;
    NTSTATUS Status = STATUS_SUCCESS;
    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH_TRY
        {
            ProbeForWrite(SecurityDescriptor, Length, sizeof(ULONG));
            if (ResultLength != NULL)
            {
            ProbeForWriteUlong(ResultLength);
        }
        }
        _SEH_HANDLE
        {
            Status = _SEH_GetExceptionCode();
        }
        _SEH_END;

        if (!NT_SUCCESS(Status)) return Status;
    }

    /* get the required access rights for the operation */
    SeQuerySecurityAccessMask(SecurityInformation,
                              &DesiredAccess);

    Status = ObReferenceObjectByHandle(Handle,
                                       DesiredAccess,
                                       NULL,
                                       PreviousMode,
                                       &Object,
                                       NULL);

    if (NT_SUCCESS(Status))
    {
        Header = OBJECT_TO_OBJECT_HEADER(Object);
        ASSERT(Header->Type != NULL);

        Status = Header->Type->TypeInfo.SecurityProcedure(
            Object,
            QuerySecurityDescriptor,
            SecurityInformation,
            SecurityDescriptor,
            &Length,
            &Header->SecurityDescriptor,
            Header->Type->TypeInfo.PoolType,
            &Header->Type->TypeInfo.GenericMapping);

        ObDereferenceObject(Object);

        /* return the required length */
        if (ResultLength != NULL)
        {
        _SEH_TRY
        {
            *ResultLength = Length;
        }
            _SEH_EXCEPT(_SEH_ExSystemExceptionFilter)
        {
            Status = _SEH_GetExceptionCode();
        }
        _SEH_END;
    }
    }

    return Status;
}

/*++
* @name NtSetSecurityObject
* @implemented NT4
*
*     The NtSetSecurityObject routine <FILLMEIN>
*
* @param Handle
*        <FILLMEIN>
*
* @param SecurityInformation
*        <FILLMEIN>
*
* @param SecurityDescriptor
*        <FILLMEIN>
*
* @return STATUS_SUCCESS or appropriate error value.
*
* @remarks None.
*
*--*/
NTSTATUS
NTAPI
NtSetSecurityObject(IN HANDLE Handle,
                    IN SECURITY_INFORMATION SecurityInformation,
                    IN PSECURITY_DESCRIPTOR SecurityDescriptor)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PVOID Object;
    POBJECT_HEADER Header;
    SECURITY_DESCRIPTOR_RELATIVE *CapturedSecurityDescriptor;
    ACCESS_MASK DesiredAccess = (ACCESS_MASK)0;
    NTSTATUS Status;
    PAGED_CODE();

    /* make sure the caller doesn't pass a NULL security descriptor! */
    if (SecurityDescriptor == NULL) return STATUS_ACCESS_DENIED;

    /* capture and make a copy of the security descriptor */
    Status = SeCaptureSecurityDescriptor(SecurityDescriptor,
                                         PreviousMode,
                                         PagedPool,
                                         TRUE,
                                         (PSECURITY_DESCRIPTOR*)
                                         &CapturedSecurityDescriptor);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Capturing the security descriptor failed! Status: 0x%lx\n", Status);
        return Status;
    }

    /*
     * make sure the security descriptor passed by the caller
     * is valid for the operation we're about to perform
     */
    if (((SecurityInformation & OWNER_SECURITY_INFORMATION) &&
         (CapturedSecurityDescriptor->Owner == 0)) ||
        ((SecurityInformation & GROUP_SECURITY_INFORMATION) &&
         (CapturedSecurityDescriptor->Group == 0)))
    {
        Status = STATUS_INVALID_SECURITY_DESCR;
    }
    else
    {
        /* get the required access rights for the operation */
        SeSetSecurityAccessMask(SecurityInformation,
                                &DesiredAccess);

        Status = ObReferenceObjectByHandle(Handle,
                                           DesiredAccess,
                                           NULL,
                                           PreviousMode,
                                           &Object,
                                           NULL);

        if (NT_SUCCESS(Status))
        {
            Header = OBJECT_TO_OBJECT_HEADER(Object);
            ASSERT(Header->Type != NULL);

            Status = Header->Type->TypeInfo.SecurityProcedure(
                Object,
                SetSecurityDescriptor,
                SecurityInformation,
                (PSECURITY_DESCRIPTOR)SecurityDescriptor,
                NULL,
                &Header->SecurityDescriptor,
                Header->Type->TypeInfo.PoolType,
                &Header->Type->TypeInfo.GenericMapping);

            ObDereferenceObject(Object);
        }
    }

    /* release the descriptor */
    SeReleaseSecurityDescriptor((PSECURITY_DESCRIPTOR)CapturedSecurityDescriptor,
                                PreviousMode,
                                TRUE);

    return Status;
}

/*++
* @name ObLogSecurityDescriptor
* @unimplemented NT5.2
*
*     The ObLogSecurityDescriptor routine <FILLMEIN>
*
* @param InputSecurityDescriptor
*        <FILLMEIN>
*
* @param OutputSecurityDescriptor
*        <FILLMEIN>
*
* @param RefBias
*        <FILLMEIN>
*
* @return STATUS_SUCCESS or appropriate error value.
*
* @remarks None.
*
*--*/
NTSTATUS
NTAPI
ObLogSecurityDescriptor(IN PSECURITY_DESCRIPTOR InputSecurityDescriptor,
                        OUT PSECURITY_DESCRIPTOR *OutputSecurityDescriptor,
                        IN ULONG RefBias)
{
    /* HACK: Return the same descriptor back */
    PISECURITY_DESCRIPTOR SdCopy;
    DPRINT1("ObLogSecurityDescriptor is not implemented!\n",
            InputSecurityDescriptor);

    SdCopy = ExAllocatePool(PagedPool, sizeof(*SdCopy));
    RtlMoveMemory(SdCopy, InputSecurityDescriptor, sizeof(*SdCopy));
    *OutputSecurityDescriptor = SdCopy;
    return STATUS_SUCCESS;
}

/*++
* @name ObDereferenceSecurityDescriptor
* @unimplemented NT5.2
*
*     The ObDereferenceSecurityDescriptor routine <FILLMEIN>
*
* @param SecurityDescriptor
*        <FILLMEIN>
*
* @param Count
*        <FILLMEIN>
*
* @return STATUS_SUCCESS or appropriate error value.
*
* @remarks None.
*
*--*/
VOID
NTAPI
ObDereferenceSecurityDescriptor(IN PSECURITY_DESCRIPTOR SecurityDescriptor,
                                IN ULONG Count)
{
    DPRINT1("ObDereferenceSecurityDescriptor is not implemented!\n");
}

/* EOF */

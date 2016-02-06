[BITS 32]
[EXTERN end]    ; End of the last loadable section - this is where we can start the heap
[EXTERN code]                   ; Start of the '.text' section.
[EXTERN bss]                    ; Start of the .bss section.
[EXTERN main]
[EXTERN symtab]
[GLOBAL FLAGS]

; Declare constants used for creating a multiboot header.
MBALIGN     equ  1<<0                   ; align loaded modules on page boundaries
MEMINFO     equ  1<<1                   ; provide memory map
VIDINFO     equ 1<<2
FLAGS       equ  MBALIGN | MEMINFO | VIDINFO      ; this is the Multiboot 'flag' field
MAGIC       equ  0x1BADB002             ; 'magic number' lets bootloader find the header
CHECKSUM    equ -(MAGIC + FLAGS)        ; checksum of above, to prove we are multiboot
 
; Declare a header as in the Multiboot Standard. We put this into a special
; section so we can force the header to be in the start of the final program.
; You don't need to understand all these details as it is just magic values that
; is documented in the multiboot standard. The bootloader will search for this
; magic sequence and recognize us as a multiboot kernel.
section .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM
    dd 0, 0, 0, 0, 0
    dd 0
    dd 640, 480, 4
 
; Currently the stack pointer register (esp) points at anything and using it may
; cause massive harm. Instead, we'll provide our own stack. We will allocate
; room for a small temporary stack by creating a symbol at the bottom of it,
; then allocating 16384 bytes for it, and finally creating a symbol at the top.
section .bootstrap_stack, nobits
align 4
stack_bottom:
resb 16384
stack_top:
 
; The linker script specifies _start as the entry point to the kernel and the
; bootloader will jump to this position once the kernel has been loaded. It
; doesn't make sense to return from this function as the bootloader is gone.
section .__mbHeader
 
align 0x4
section .text
global _start
_start:
    ; Welcome to kernel mode! We now have sufficient code for the bootloader to
    ; load and run our operating system. It doesn't do anything interesting yet.
    ; Perhaps we would like to call printf("Hello, World\n"). You should now
    ; realize one of the profound truths about kernel mode: There is nothing
    ; there unless you provide it yourself. There is no printf function. There
    ; is no <stdio.h> header. If you want a function, you will have to code it
    ; yourself. And that is one of the best things about kernel development:
    ; you get to make the entire system yourself. You have absolute and complete
    ; power over the machine, there are no security restrictions, no safe
    ; guards, no debugging mechanisms, there is nothing but what you build.
 
    ; By now, you are perhaps tired of assembly language. You realize some
    ; things simply cannot be done in C, such as making the multiboot header in
    ; the right section and setting up the stack. However, you would like to
    ; write the operating system in a higher level language, such as C or C++.
    ; To that end, the next task is preparing the processor for execution of
    ; such code. C doesn't expect much at this point and we only need to set up
    ; a stack. Note that the processor is not fully initialized yet and stuff
    ; such as floating point instructions are not available yet.
 
    push esp        ; We push the stack pointer, so we know the beginning of the stack

    call main
    jmp $
 
    ; In case the function returns, we'll want to put the computer into an
    ; infinite loop. To do that, we use the clear interrupt ('cli') instruction
    ; to disable interrupts, the halt instruction ('hlt') to stop the CPU until
    ; the next interrupt arrives, and jumping to the halt instruction if it ever
    ; continues execution, just to be safe.
    cli
.hang:
    hlt
    jmp .hang
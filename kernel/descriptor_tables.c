#include "libc.h"
#include "kernel.h"
#include "descriptor_tables.h"
#include "isr.h"

// Lets us access our ASM functions from our C code.
extern void gdt_flush(uint);
extern void idt_flush(uint);

// Internal function prototypes.
static void init_gdt();
static void init_idt();
static void gdt_set_gate(sint32,uint,uint,uint8,uint8);
static void idt_set_gate(uint8,uint,uint16,uint8);

// This structure contains the value of one GDT entry.
// We use the attribute 'packed' to tell GCC not to change
// any of the alignment in the structure.
struct gdt_entry_struct
{
    uint16 limit_low;           // The lower 16 bits of the limit.
    uint16 base_low;            // The lower 16 bits of the base.
    uint8  base_middle;         // The next 8 bits of the base.
    uint8  access;              // Access flags, determine what ring this segment can be used in.
    uint8  granularity;
    uint8  base_high;           // The last 8 bits of the base.
} __attribute__((packed));

struct gdt_entry_bits
{
  unsigned int limit_low:16;
  unsigned int base_low : 24;
     //attribute byte split into bitfields
  unsigned int accessed :1;
  unsigned int read_write :1; //readable for code, writable for data
  unsigned int conforming_expand_down :1; //conforming for code, expand down for data
  unsigned int code :1; //1 for code, 0 for data
  unsigned int always_1 :1; //should be 1 for everything but TSS and LDT
  unsigned int DPL :2; //priviledge level
  unsigned int present :1;
     //and now into granularity
  unsigned int limit_high :4;
  unsigned int available :1;
  unsigned int always_0 :1; //should always be 0
  unsigned int big :1; //32bit opcodes for code, uint32_t stack for data
  unsigned int gran :1; //1 to use 4k page addressing, 0 for byte addressing
  unsigned int base_high :8;
} __packed; //or __attribute__((packed))

typedef struct gdt_entry_struct gdt_entry_t;

// This struct describes a GDT pointer. It points to the start of
// our array of GDT entries, and is in the format required by the
// lgdt instruction.
struct gdt_ptr_struct
{
    uint16 limit;               // The upper 16 bits of all selector limits.
    uint base;                // The address of the first gdt_entry_t struct.
} __attribute__((packed));

typedef struct gdt_ptr_struct gdt_ptr_t;

// A struct describing an interrupt gate.
struct idt_entry_struct
{
    uint16 base_lo;             // The lower 16 bits of the address to jump to when this interrupt fires.
    uint16 sel;                 // Kernel segment selector.
    uint8  always0;             // This must always be zero.
    uint8  flags;               // More flags. See documentation.
    uint16 base_hi;             // The upper 16 bits of the address to jump to.
} __attribute__((packed));

typedef struct idt_entry_struct idt_entry_t;

// A struct describing a pointer to an array of interrupt handlers.
// This is in a format suitable for giving to 'lidt'.
struct idt_ptr_struct
{
    uint16 limit;
    uint base;                // The address of the first element in our idt_entry_t array.
} __attribute__((packed));

typedef struct idt_ptr_struct idt_ptr_t;

typedef struct __attribute__((packed))
{
   uint prev_tss;   // The previous TSS - if we used hardware task switching this would form a linked list.
   uint esp0;       // The stack pointer to load when we change to kernel mode.
   uint ss0;        // The stack segment to load when we change to kernel mode.
   uint esp1;       // Unused...
   uint ss1;
   uint esp2;
   uint ss2;
   uint cr3;
   uint eip;
   uint eflags;
   uint eax;
   uint ecx;
   uint edx;
   uint ebx;
   uint esp;
   uint ebp;
   uint esi;
   uint edi;
   uint es;         // The value to load into ES when we change to kernel mode.
   uint cs;         // The value to load into CS when we change to kernel mode.
   uint ss;         // The value to load into SS when we change to kernel mode.
   uint ds;         // The value to load into DS when we change to kernel mode.
   uint fs;         // The value to load into FS when we change to kernel mode.
   uint gs;         // The value to load into GS when we change to kernel mode.
   uint ldt;        // Unused...
   uint16 trap;
   uint16 iomap_base;
} TSSEntry;

extern void tss_flush();
static void write_tss(int,uint16,uint);

TSSEntry tss_entry;

// These extern directives let us access the addresses of our ASM ISR handlers.
extern void isr0 ();    // Division by zero
extern void isr1 ();    // Debug
extern void isr2 ();    // Non Maskable Interrupt
extern void isr3 ();    // Breakpoint
extern void isr4 ();    // Into Detected Overflow
extern void isr5 ();    // Out of Bounds
extern void isr6 ();    // Invalid Opcode
extern void isr7 ();    // No Coprocessor
extern void isr8 ();    // Double Fault
extern void isr9 ();    // Coprocessor Segment Overrun
extern void isr10();    // Bad TSS
extern void isr11();    // Segment Not Present
extern void isr12();    // Stack Fault
extern void isr13();    // General Protection Fault
extern void isr14();    // Page Fault
extern void isr15();    // Unknown Interrupt
extern void isr16();    // Coprocessor Fault
extern void isr17();    // Alignment Check
extern void isr18();    // Machine Check
extern void isr19();
extern void isr20();
extern void isr21();
extern void isr22();
extern void isr23();
extern void isr24();
extern void isr25();
extern void isr26();
extern void isr27();
extern void isr28();
extern void isr29();
extern void isr30();
extern void isr31();
extern void isr128();
extern void irq0 ();
extern void irq1 ();
extern void irq2 ();
extern void irq3 ();
extern void irq4 ();
extern void irq5 ();
extern void irq6 ();
extern void irq7 ();
extern void irq8 ();
extern void irq9 ();
extern void irq10();
extern void irq11();
extern void irq12();
extern void irq13();
extern void irq14();
extern void irq15();

gdt_entry_t gdt_entries[6];
gdt_ptr_t   gdt_ptr;
idt_entry_t idt_entries[256];
idt_ptr_t   idt_ptr;

// Extern the ISR handler array so we can nullify them on startup.
extern isr_t interrupt_handlers[];

// Initialisation routine - zeroes all the interrupt service routines,
// initialises the GDT and IDT.
void init_descriptor_tables()
{

    // Initialise the global descriptor table.
    init_gdt();
    // Initialise the interrupt descriptor table.
    init_idt();
    // Nullify all the interrupt handlers.
    memset((void*)&interrupt_handlers, 0, sizeof(isr_t)*256);
}

static void init_gdt()
{
    gdt_ptr.limit = (sizeof(gdt_entry_t) * 6) - 1;
    gdt_ptr.base  = (uint)&gdt_entries;

    gdt_set_gate(0, 0, 0, 0, 0);                // Null segment
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); // Code segment
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); // Data segment
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF); // User mode code segment
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); // User mode data segment
    write_tss(5, 0x10, 0x10);

    gdt_flush((uint)&gdt_ptr);
    tss_flush();
}

// Initialise our task state segment structure.
static void write_tss(int num, uint16 ss0, uint esp0)
{
   // Firstly, let's compute the base and limit of our entry into the GDT.
   uint base = (uint) &tss_entry;
   uint limit = base + sizeof(tss_entry);

   // Now, add our TSS descriptor's address to the GDT.
   gdt_set_gate(num, base, limit, 0xE9, 0x00);

   // Ensure the descriptor is initially zero.
   memset(&tss_entry, 0, sizeof(tss_entry));

   tss_entry.ss0  = ss0;  // Set the kernel stack segment.
   tss_entry.esp0 = esp0; // Set the kernel stack pointer.

   // Here we set the cs, ss, ds, es, fs and gs entries in the TSS. These specify what
   // segments should be loaded when the processor switches to kernel mode. Therefore
   // they are just our normal kernel code/data segments - 0x08 and 0x10 respectively,
   // but with the last two bits set, making 0x0b and 0x13. The setting of these bits
   // sets the RPL (requested privilege level) to 3, meaning that this TSS can be used
   // to switch to kernel mode from ring 3.
   tss_entry.cs   = 0x0b;
   tss_entry.ss = tss_entry.ds = tss_entry.es = tss_entry.fs = tss_entry.gs = 0x13;
}

void set_kernel_stack(void *stack)
{
   tss_entry.esp0 = (uint)stack;
}

// Set the value of one GDT entry.
static void gdt_set_gate(int num, uint base, uint limit, uint8 access, uint8 gran)
{
    gdt_entries[num].base_low    = (base & 0xFFFF);
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high   = (base >> 24) & 0xFF;

    gdt_entries[num].limit_low   = (limit & 0xFFFF);
    gdt_entries[num].granularity = (limit >> 16) & 0x0F;
    
    gdt_entries[num].granularity |= gran & 0xF0;
    gdt_entries[num].access      = access;
}

static void init_idt()
{
    idt_ptr.limit = sizeof(idt_entry_t) * 256 -1;
    idt_ptr.base  = (uint)&idt_entries;

    memset(&idt_entries, 0, sizeof(idt_entry_t)*256);

    // Remap the irq table.
    outportb(0x20, 0x11);
    outportb(0xA0, 0x11);
    outportb(0x21, 0x20);
    outportb(0xA1, 0x28);
    outportb(0x21, 0x04);
    outportb(0xA1, 0x02);
    outportb(0x21, 0x01);
    outportb(0xA1, 0x01);
    outportb(0x21, 0x0);
    outportb(0xA1, 0x0);

    idt_set_gate( 0, (uint)isr0 , 0x08, 0x8E);
    idt_set_gate( 1, (uint)isr1 , 0x08, 0x8E);
    idt_set_gate( 2, (uint)isr2 , 0x08, 0x8E);
    idt_set_gate( 3, (uint)isr3 , 0x08, 0x8E);
    idt_set_gate( 4, (uint)isr4 , 0x08, 0x8E);
    idt_set_gate( 5, (uint)isr5 , 0x08, 0x8E);
    idt_set_gate( 6, (uint)isr6 , 0x08, 0x8E);
    idt_set_gate( 7, (uint)isr7 , 0x08, 0x8E);
    idt_set_gate( 8, (uint)isr8 , 0x08, 0x8E);
    idt_set_gate( 9, (uint)isr9 , 0x08, 0x8E);
    idt_set_gate(10, (uint)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint)isr16, 0x08, 0x8E);
    idt_set_gate(17, (uint)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint)isr18, 0x08, 0x8E);
    idt_set_gate(19, (uint)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint)isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint)isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint)isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint)isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint)isr28, 0x08, 0x8E);
    idt_set_gate(29, (uint)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint)isr30, 0x08, 0x8E);
    idt_set_gate(31, (uint)isr31, 0x08, 0x8E);
    idt_set_gate(32, (uint)irq0, 0x08, 0x8E);
    idt_set_gate(33, (uint)irq1, 0x08, 0x8E);
    idt_set_gate(34, (uint)irq2, 0x08, 0x8E);
    idt_set_gate(35, (uint)irq3, 0x08, 0x8E);
    idt_set_gate(36, (uint)irq4, 0x08, 0x8E);
    idt_set_gate(37, (uint)irq5, 0x08, 0x8E);
    idt_set_gate(38, (uint)irq6, 0x08, 0x8E);
    idt_set_gate(39, (uint)irq7, 0x08, 0x8E);
    idt_set_gate(40, (uint)irq8, 0x08, 0x8E);
    idt_set_gate(41, (uint)irq9, 0x08, 0x8E);
    idt_set_gate(42, (uint)irq10, 0x08, 0x8E);
    idt_set_gate(43, (uint)irq11, 0x08, 0x8E);
    idt_set_gate(44, (uint)irq12, 0x08, 0x8E);
    idt_set_gate(45, (uint)irq13, 0x08, 0x8E);
    idt_set_gate(46, (uint)irq14, 0x08, 0x8E);
    idt_set_gate(47, (uint)irq15, 0x08, 0x8E);
    idt_set_gate(128, (uint)isr128, 0x08, 0x8E);

    idt_flush((uint)&idt_ptr);
}

static void idt_set_gate(uint8 num, uint base, uint16 sel, uint8 flags)
{
    idt_entries[num].base_lo = base & 0xFFFF;
    idt_entries[num].base_hi = (base >> 16) & 0xFFFF;

    idt_entries[num].sel     = sel;
    idt_entries[num].always0 = 0;
    // We must uncomment the OR below when we get to using user-mode.
    // It sets the interrupt gate's privilege level to 3.
    idt_entries[num].flags   = flags | 0x60;
}

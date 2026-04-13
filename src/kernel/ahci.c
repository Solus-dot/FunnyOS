#include "ahci.h"
#include "io.h"
#include "kstring.h"
#include "memory.h"
#include "paging.h"

#define AHCI_PCI_CLASS_MASS_STORAGE 0x01u
#define AHCI_PCI_SUBCLASS_SATA 0x06u
#define AHCI_PCI_PROGIF_AHCI 0x01u
#define AHCI_PCI_COMMAND_MEMORY_SPACE 0x0002u
#define AHCI_PCI_COMMAND_BUS_MASTER 0x0004u

#define AHCI_BAR_INDEX 5u
#define AHCI_MMIO_WINDOW_SIZE 0x2000u
#define AHCI_PORT_COUNT 32u

#define HBA_GHC_AE (1u << 31)
#define HBA_PXIS_TFES (1u << 30)
#define HBA_PXCMD_ST (1u << 0)
#define HBA_PXCMD_SUD (1u << 1)
#define HBA_PXCMD_FRE (1u << 4)
#define HBA_PXCMD_FR (1u << 14)
#define HBA_PXCMD_CR (1u << 15)
#define HBA_PXTFD_DRQ 0x08u
#define HBA_PXTFD_BSY 0x80u
#define HBA_PXSSTS_DET_PRESENT 0x03u
#define HBA_PXSSTS_IPM_ACTIVE 0x01u
#define SATA_SIG_ATA 0x00000101u

#define FIS_TYPE_REG_H2D 0x27u
#define ATA_CMD_READ_DMA_EXT 0x25u
#define ATA_CMD_WRITE_DMA_EXT 0x35u

#define AHCI_MAX_PRDT_BYTES (4u * 1024u * 1024u)
#define AHCI_COMMAND_SLOT 0u

typedef struct HbaPort {
    uint32_t clb;
    uint32_t clbu;
    uint32_t fb;
    uint32_t fbu;
    uint32_t is;
    uint32_t ie;
    uint32_t cmd;
    uint32_t reserved0;
    uint32_t tfd;
    uint32_t sig;
    uint32_t ssts;
    uint32_t sctl;
    uint32_t serr;
    uint32_t sact;
    uint32_t ci;
    uint32_t sntf;
    uint32_t fbs;
    uint32_t reserved1[11];
    uint32_t vendor[4];
} HbaPort;

typedef struct HbaMemory {
    uint32_t cap;
    uint32_t ghc;
    uint32_t is;
    uint32_t pi;
    uint32_t vs;
    uint32_t ccc_ctl;
    uint32_t ccc_pts;
    uint32_t em_loc;
    uint32_t em_ctl;
    uint32_t cap2;
    uint32_t bohc;
    uint8_t reserved[0x74];
    uint8_t vendor[0x60];
    HbaPort ports[AHCI_PORT_COUNT];
} HbaMemory;

typedef struct HbaCommandHeader {
    uint8_t flags;
    uint8_t flags2;
    uint16_t prdtl;
    uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t reserved[4];
} HbaCommandHeader;

typedef struct HbaPrdtEntry {
    uint32_t dba;
    uint32_t dbau;
    uint32_t reserved;
    uint32_t dbc;
} HbaPrdtEntry;

typedef struct HbaCommandTable {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t reserved[48];
    HbaPrdtEntry prdt[1];
} HbaCommandTable;

typedef struct FisRegH2D {
    uint8_t fis_type;
    uint8_t pmport_c;
    uint8_t command;
    uint8_t featurel;
    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t device;
    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t featureh;
    uint8_t countl;
    uint8_t counth;
    uint8_t icc;
    uint8_t control;
    uint8_t reserved[4];
} FisRegH2D;

static volatile HbaMemory* g_hba = NULL;
static volatile HbaPort* g_port = NULL;
static HbaCommandHeader* g_command_list = NULL;
static uint8_t* g_received_fis = NULL;
static HbaCommandTable* g_command_table = NULL;

static bool ahci_wait_port_mask_clear(volatile HbaPort* port, uint32_t mask)
{
    uint32_t spins;

    for (spins = 0u; spins < 1000000u; ++spins) {
        if ((port->cmd & mask) == 0u)
            return true;
        cpu_pause();
    }

    return false;
}

static bool ahci_wait_port_ready(volatile HbaPort* port)
{
    uint32_t spins;

    for (spins = 0u; spins < 1000000u; ++spins) {
        if ((port->tfd & (HBA_PXTFD_BSY | HBA_PXTFD_DRQ)) == 0u)
            return true;
        cpu_pause();
    }

    return false;
}

static void ahci_stop_port(volatile HbaPort* port)
{
    port->cmd &= (uint32_t)~HBA_PXCMD_ST;
    port->cmd &= (uint32_t)~HBA_PXCMD_FRE;
    (void)ahci_wait_port_mask_clear(port, HBA_PXCMD_FR | HBA_PXCMD_CR);
}

static void ahci_start_port(volatile HbaPort* port)
{
    (void)ahci_wait_port_mask_clear(port, HBA_PXCMD_CR);
    port->cmd |= HBA_PXCMD_FRE | HBA_PXCMD_SUD;
    port->cmd |= HBA_PXCMD_ST;
}

static bool ahci_port_has_sata_drive(volatile HbaPort* port)
{
    uint32_t ssts = port->ssts;
    uint8_t det = (uint8_t)(ssts & 0x0Fu);
    uint8_t ipm = (uint8_t)((ssts >> 8u) & 0x0Fu);

    if (det != HBA_PXSSTS_DET_PRESENT || ipm != HBA_PXSSTS_IPM_ACTIVE)
        return false;
    return port->sig == SATA_SIG_ATA;
}

static bool ahci_prepare_memory(void)
{
    if (g_command_list == NULL) {
        g_command_list = (HbaCommandHeader*)alloc_pages(1u);
        if (g_command_list == NULL)
            return false;
    }
    if (g_received_fis == NULL) {
        g_received_fis = (uint8_t*)alloc_pages(1u);
        if (g_received_fis == NULL)
            return false;
    }
    if (g_command_table == NULL) {
        g_command_table = (HbaCommandTable*)alloc_pages(1u);
        if (g_command_table == NULL)
            return false;
    }

    k_memset(g_command_list, 0, 4096u);
    k_memset(g_received_fis, 0, 4096u);
    k_memset(g_command_table, 0, 4096u);
    return true;
}

static bool ahci_configure_port(volatile HbaPort* port)
{
    HbaCommandHeader* header;

    if (!ahci_prepare_memory())
        return false;

    ahci_stop_port(port);

    port->clb = (uint32_t)(uintptr_t)g_command_list;
    port->clbu = 0u;
    port->fb = (uint32_t)(uintptr_t)g_received_fis;
    port->fbu = 0u;
    port->is = 0xFFFFFFFFu;
    port->ie = 0u;
    port->serr = 0xFFFFFFFFu;
    port->sact = 0u;
    port->ci = 0u;

    header = &g_command_list[AHCI_COMMAND_SLOT];
    header->flags = (uint8_t)(sizeof(FisRegH2D) / sizeof(uint32_t));
    header->flags2 = 0u;
    header->prdtl = 1u;
    header->prdbc = 0u;
    header->ctba = (uint32_t)(uintptr_t)g_command_table;
    header->ctbau = 0u;

    ahci_start_port(port);
    return ahci_wait_port_ready(port);
}

static bool ahci_issue_io(uint32_t lba, uint8_t count, void* buffer, bool write)
{
    HbaCommandHeader* header;
    HbaCommandTable* table;
    HbaPrdtEntry* prdt;
    FisRegH2D* fis;
    uint32_t byte_count;
    uint32_t spins;

    if (g_port == NULL)
        return false;
    if (count == 0u)
        return true;

    byte_count = (uint32_t)count * 512u;
    if (byte_count == 0u || byte_count > AHCI_MAX_PRDT_BYTES)
        return false;
    if ((uintptr_t)buffer >> 32u != 0u)
        return false;
    if (!ahci_wait_port_ready(g_port))
        return false;

    header = &g_command_list[AHCI_COMMAND_SLOT];
    table = g_command_table;
    prdt = &table->prdt[0];
    fis = (FisRegH2D*)table->cfis;

    k_memset(table, 0, sizeof(*table));
    header->flags = (uint8_t)(sizeof(FisRegH2D) / sizeof(uint32_t));
    if (write)
        header->flags |= (uint8_t)(1u << 6u);
    header->flags2 = 0u;
    header->prdtl = 1u;
    header->prdbc = 0u;

    prdt->dba = (uint32_t)(uintptr_t)buffer;
    prdt->dbau = 0u;
    prdt->reserved = 0u;
    prdt->dbc = byte_count - 1u;

    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->pmport_c = (uint8_t)(1u << 7u);
    fis->command = write ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT;
    fis->featurel = 0u;
    fis->lba0 = (uint8_t)(lba & 0xFFu);
    fis->lba1 = (uint8_t)((lba >> 8u) & 0xFFu);
    fis->lba2 = (uint8_t)((lba >> 16u) & 0xFFu);
    fis->device = (uint8_t)(1u << 6u);
    fis->lba3 = (uint8_t)((lba >> 24u) & 0xFFu);
    fis->lba4 = 0u;
    fis->lba5 = 0u;
    fis->featureh = 0u;
    fis->countl = count;
    fis->counth = 0u;
    fis->icc = 0u;
    fis->control = 0u;

    g_port->is = 0xFFFFFFFFu;
    g_port->ci = (uint32_t)(1u << AHCI_COMMAND_SLOT);

    for (spins = 0u; spins < 1000000u; ++spins) {
        if ((g_port->ci & (uint32_t)(1u << AHCI_COMMAND_SLOT)) == 0u)
            break;
        if ((g_port->is & HBA_PXIS_TFES) != 0u)
            return false;
        cpu_pause();
    }

    if ((g_port->ci & (uint32_t)(1u << AHCI_COMMAND_SLOT)) != 0u)
        return false;
    if ((g_port->is & HBA_PXIS_TFES) != 0u)
        return false;
    return true;
}

bool ahci_init(const AhciDeviceAddress* device)
{
    PciDeviceInfo info;
    uint16_t command;
    uint64_t abar;
    uintptr_t abar_base;
    uint32_t port_mask;

    if (device == NULL)
        return false;
    if (device->port >= AHCI_PORT_COUNT)
        return false;
    if (!pci_read_device_info(&device->controller, &info))
        return false;
    if (info.class_code != AHCI_PCI_CLASS_MASS_STORAGE
        || info.subclass != AHCI_PCI_SUBCLASS_SATA
        || info.prog_if != AHCI_PCI_PROGIF_AHCI)
        return false;

    command = pci_read_config16(&device->controller, 0x04u);
    command |= AHCI_PCI_COMMAND_MEMORY_SPACE | AHCI_PCI_COMMAND_BUS_MASTER;
    pci_write_config16(&device->controller, 0x04u, command);

    abar = pci_read_bar64(&device->controller, AHCI_BAR_INDEX);
    if ((abar & 0x1u) != 0u)
        return false;

    abar_base = (uintptr_t)(abar & ~0x0Full);
    if (abar_base < 0x1000u)
        return false;
    if (!paging_map_range(abar_base, abar_base, AHCI_MMIO_WINDOW_SIZE, true, false))
        return false;

    g_hba = (volatile HbaMemory*)abar_base;
    g_hba->ghc |= HBA_GHC_AE;

    port_mask = (uint32_t)(1u << device->port);
    if ((g_hba->pi & port_mask) == 0u)
        return false;

    g_port = &g_hba->ports[device->port];
    if (!ahci_port_has_sata_drive(g_port))
        return false;
    if (!ahci_configure_port(g_port))
        return false;
    return true;
}

bool ahci_read_sectors(uint32_t lba, uint8_t count, void* out)
{
    return ahci_issue_io(lba, count, out, false);
}

bool ahci_write_sectors(uint32_t lba, uint8_t count, const void* data)
{
    return ahci_issue_io(lba, count, (void*)data, true);
}

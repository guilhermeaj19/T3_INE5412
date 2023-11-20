#include "fs.h"

int INE5412_FS::fs_format() {
    if (is_mounted) return 0;

    int nblocks = disk->size();
    int ninodeblocks;

    if (nblocks % 10) {
        ninodeblocks = nblocks / 10 + 1;
    } else {
        ninodeblocks = nblocks / 10;
    }

    union fs_block block;

    for (int i = 0; i < INODES_PER_BLOCK; i++) {
        block.inode[i].isvalid = 0;
    }

    for (int i = 1; i < nblocks; i++) {
        disk->write(i, block.data);
    }

    int ninodes = ninodeblocks * INODES_PER_BLOCK;

    block.super.magic = FS_MAGIC;
    block.super.nblocks = nblocks;
    block.super.ninodeblocks = ninodeblocks;
    block.super.ninodes = ninodes;

    disk->write(0, block.data);

    return 1;
}

void INE5412_FS::fs_debug() {
    union fs_block block;

    disk->read(0, block.data);

    cout << "superblock:\n";
    cout << "    " << (block.super.magic == FS_MAGIC ? "magic number is valid\n" : "magic number is invalid!\n");
    cout << "    " << block.super.nblocks << " blocks\n";
    cout << "    " << block.super.ninodeblocks << " inode blocks\n";
    cout << "    " << block.super.ninodes << " inodes\n";

    for (int i = 0; i < block.super.ninodeblocks + 1; i++) {
        disk->read(i + 1, block.data);

        for (int j = 0; j < INODES_PER_BLOCK; j++) {
            fs_inode inode = block.inode[j];

            if (inode.isvalid) {
                cout << "inode " << i * INODES_PER_BLOCK + j + 1 << ":" << endl;
                cout << "    " << "size: " << inode.size << " bytes" << endl;
                if (inode.size > 0) {
                    cout << "    " << "direct blocks: ";
                    for (int k = 0; k < POINTERS_PER_INODE; k++) {
                        if (inode.direct[k] != 0)
                            cout << inode.direct[k] << " ";
                    }
                    cout << endl;

                    if (inode.indirect != 0) {
                        cout << "    " << "indirect block: " << inode.indirect << endl;

                        union fs_block indirect;
                        disk->read(inode.indirect, indirect.data);

                        std::vector<int> data_blocks;

                        for (int k = 0; k < POINTERS_PER_BLOCK; k++) {
                            if (indirect.pointers[k] != 0) {
                                data_blocks.push_back(indirect.pointers[k]);
                            }
                        }
                        if (data_blocks.size() > 0) {
                            cout << "    " << "indirect data blocks: ";
                            for (size_t k = 0; k < data_blocks.size(); k++) {
                                cout << data_blocks.at(k) << " ";
                            }
                            cout << endl;
                        }
                    }
                }
            }
        }
    }
}

int INE5412_FS::fs_mount() {
    union fs_block block;

    if (is_mounted) {
        return 0;
    }
    disk->read(0, block.data);

    if (block.super.magic != FS_MAGIC) {
        return 0;
    }

    for (int i = 0; i < block.super.nblocks; i++) {
        bitmap.push_back(0);
    }

    bitmap[0] = 1;

    int ninodeblocks = block.super.ninodeblocks;

    for (int i = 0; i < ninodeblocks; i++) {
        disk->read(i + 1, block.data);
        bitmap[i + 1] = 1;

        for (int j = 0; j < INODES_PER_BLOCK; j++) {
            fs_inode inode = block.inode[j];

            if (inode.isvalid) {
                for (int k = 0; k < POINTERS_PER_INODE; k++) {
                    if (inode.direct[k] != 0) bitmap[inode.direct[k]] = 1;
                }

                if (inode.indirect != 0) {
                    bitmap[inode.indirect] = 1;

                    union fs_block indirect;
                    disk->read(inode.indirect, indirect.data);

                    std::vector<int> data_blocks;

                    for (int k = 0; k < POINTERS_PER_BLOCK; k++) {
                        if (indirect.pointers[k] != 0) {
                            bitmap[indirect.pointers[k]] = 1;
                        }
                    }
                }
            }
        }
    }

    is_mounted = true;
    // for (size_t i = 0; i < bitmap.size(); i++) {
    // 	cout << "Block " << i << ": " << bitmap[i] << endl;
    // }
    // Construir o bitmap: cria uma lista de booleanos e verifica quais blocos
    // estão livres O bloco zero até ninodeblocks - 1 são restritos, logo nunca
    // serão livres. Demais blocos analisar pelos inodes válidos (blocos
    // diretos, bloco indireto e blocos de dados indiretos não são livres) se
    // der certo o atributo is_mounted fica true, se não fica false

    return 1;
}

int INE5412_FS::fs_create() {
    union fs_block block;

	if (not is_mounted) return 0;

    disk->read(0, block.data);

    int ninodeblocks = block.super.ninodeblocks;

    for (int inode_block = 0; inode_block < ninodeblocks; inode_block++) {
        disk->read(inode_block + 1, block.data);
        for (int inode = 0; inode < INODES_PER_BLOCK; inode++) {
            if (not block.inode[inode].isvalid) {
                block.inode[inode].isvalid = 1;
                block.inode[inode].size = 0;
                disk->write(inode_block + 1, block.data);
                return inode_block * INODES_PER_BLOCK + inode + 1;
            }
        }
    }

    return 0;
}

int INE5412_FS::fs_delete(int inumber) {
    inumber--;

	if (not is_mounted) return 0;

	union fs_block block;

	disk->read(0, block.data);
    if (inumber < 0 || block.super.ninodes < inumber) {
        return 0;
    }

    // Pega o bloco no disco referente ao inumber
    int inode_block = inumber / INODES_PER_BLOCK + 1;

    // Lê o inode block e pega o inode relativo ao bloco
    disk->read(inode_block, block.data);

    fs_inode inode = block.inode[inumber % INODES_PER_BLOCK];

	if (not inode.isvalid)
		return 0;

	union fs_block block2 = block;

	for (int i = 0; i < POINTERS_PER_INODE; i++) {
		if (inode.direct[i] != 0) {
			bitmap[inode.direct[i]] = 0;
		}
	}

	if (inode.indirect != 0) {
		disk->read(inode.indirect, block.data);

		for (int i = 0; i < POINTERS_PER_BLOCK; i++) {
			if (block.pointers[i] != 0) {
				bitmap[block.pointers[i]] = 0;
			}
		}

		bitmap[inode.indirect] = 0;
	}
	inode.isvalid = false;
	block2.inode[inumber % INODES_PER_BLOCK] = inode;
	disk->write(inode_block, block2.data);
    return 1;
}

int INE5412_FS::fs_getsize(int inumber) {

    if (not is_mounted) return -1;

    inumber--;

    union fs_block block;

    disk->read(0, block.data);
    if (inumber < 0 || block.super.ninodes < inumber) {
        return -1;
    }

    // Pega o bloco no disco referente ao inumber
    int inode_block = inumber / INODES_PER_BLOCK + 1;

    // Lê o inode block e pega o inode relativo ao bloco
    disk->read(inode_block, block.data);
    fs_inode inode = block.inode[inumber % INODES_PER_BLOCK];

    // Se o inode é válido, retorna seu tamanho
    if (inode.isvalid) {
        return inode.size;
    }
    return -1;
}

int INE5412_FS::fs_read(int inumber, char *data, int length, int offset) {
    
    if (not is_mounted) return -1;

    inumber--;

    union fs_block block;

    disk->read(0, block.data);
    if (inumber < 0 || block.super.ninodes < inumber) {
        return -1;
    }

    // Pega o bloco no disco referente ao inumber
    int inode_block = inumber / INODES_PER_BLOCK + 1;

    // Lê o inode block e pega o inode relativo ao bloco
    disk->read(inode_block, block.data);
    fs_inode inode = block.inode[inumber % INODES_PER_BLOCK];

    if (not inode.isvalid) return -1;

    int init_block = offset/Disk::DISK_BLOCK_SIZE;
    int pos_in_block = offset % Disk::DISK_BLOCK_SIZE;

    if(offset >= inode.size) return -1;

    if (init_block >= POINTERS_PER_INODE) {
        disk->read(inode.indirect, block.data);
        disk->read(block.pointers[init_block-POINTERS_PER_INODE], block.data);
    } else {
        disk->read(inode.direct[init_block], block.data);
    }

    for (int i = 0; i < length; i++) {
        if (offset + i >= inode.size) {
            return i;
        }
        if (pos_in_block == Disk::DISK_BLOCK_SIZE) {
            if (init_block + 1 >= POINTERS_PER_INODE) {
                init_block++;
                disk->read(inode.indirect, block.data);
                disk->read(block.pointers[init_block-POINTERS_PER_INODE], block.data);
            } else {
                init_block++;
                disk->read(inode.direct[init_block], block.data);
            }
            pos_in_block = 0;
        }

        data[i] = block.data[pos_in_block];
        pos_in_block++;

    }

    return length;
}

int INE5412_FS::fs_write(int inumber, const char *data, int length,
                         int offset) {
    // Inodo size igual à soma de todos os blocos relacionados a ele?
    return 0;
}

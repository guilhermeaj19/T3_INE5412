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

    for (int i = 0; i < bitmap.size(); i++) {
        cout << "Block " << i << ": " << bitmap[i] << endl;
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

	if (not is_mounted) return 0;

    fs_inode inode;
    if (not inode_load(inumber, &inode)) {
        return 0;
    }

	if (not inode.isvalid)
		return 0;

	for (int i = 0; i < POINTERS_PER_INODE; i++) {
		if (inode.direct[i] != 0) {
			bitmap[inode.direct[i]] = 0;
		}
	}
    union fs_block block;

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
	inode_save(inumber, &inode);
    return 1;
}

int INE5412_FS::fs_getsize(int inumber) {

    if (not is_mounted) return -1;

    fs_inode inode;
    if (not inode_load(inumber, &inode)) {
        return -1;
    }

    // Se o inode é válido, retorna seu tamanho
    if (inode.isvalid) {
        return inode.size;
    }
    return -1;
}

int INE5412_FS::fs_read(int inumber, char *data, int length, int offset) {
    
    if (not is_mounted) return -1;

    fs_inode inode;
    
    // Se não conseguiu carregar o inode, não é possível ler o arquivo
    if (not inode_load(inumber, &inode)) {
        return -1;
    }

    // Se o inode não é válido, não há arquivo a ser lido
    if (not inode.isvalid) {
        return -1;
    }

    // Se o offset é maior que o tamanho do inodo (em bytes), quer dizer que está buscando uma posição inválida
    if(offset >= inode.size) {
        return -1;
    }

    int num_block = offset/Disk::DISK_BLOCK_SIZE; //Bloco inicial
    int pos_in_block = offset % Disk::DISK_BLOCK_SIZE; //Posicao inicial no bloco inicial 

    union fs_block block;

    // Verificação inicial para identificar se está num bloco direto ou indireto
    if (num_block >= POINTERS_PER_INODE) {
        disk->read(inode.indirect, block.data);
        disk->read(block.pointers[num_block-POINTERS_PER_INODE], block.data);
    } else {
        disk->read(inode.direct[num_block], block.data);
    }

    // Escrita sequencial do inodo no char*data recebido
    for (int i = 0; i < length; i++) {
        // Se durante a escrita de um bloco chegar no fim do arquivo, volta a quantidade lida < length
        if (offset + i >= inode.size) {
            return i;
        }

        //Semelhante à verificação inicial, apenas alterando o número do próximo bloco
        if (pos_in_block == Disk::DISK_BLOCK_SIZE) {
            if (num_block + 1 >= POINTERS_PER_INODE) {
                num_block++;
                disk->read(inode.indirect, block.data);
                disk->read(block.pointers[num_block-POINTERS_PER_INODE], block.data);
            } else {
                num_block++;
                disk->read(inode.direct[num_block], block.data);
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
    if (not is_mounted) return -1;

    fs_inode inode;
    
    // Se não conseguiu carregar o inode, não é possível ler o arquivo
    if (not inode_load(inumber, &inode)) {
        return -1;
    }

    // Se o inode não é válido, não há arquivo a ser lido
    if (not inode.isvalid) {
        return -1;
    }

    int num_block = offset/Disk::DISK_BLOCK_SIZE; //Bloco inicial
    int pos_in_block = offset % Disk::DISK_BLOCK_SIZE; //Posicao inicial no bloco inicial 

    union fs_block block;
    int next_block;
    // Verificação inicial para identificar se está num bloco direto ou indireto
    if (num_block >= POINTERS_PER_INODE) {
        if (inode.indirect != 0) {
                num_block++;
                if (num_block-POINTERS_PER_INODE > INODES_PER_BLOCK) {
                    return -1;
                }
                disk->read(inode.indirect, block.data);
                if (block.pointers[num_block-POINTERS_PER_INODE] != 0) {
                    next_block = block.pointers[num_block-POINTERS_PER_INODE];
                    disk->read(block.pointers[num_block-POINTERS_PER_INODE], block.data);
                } else {
                    next_block = next_free_block();
                    if (next_block == 0) {
                        return -1;
                    };

                    block.pointers[num_block-POINTERS_PER_INODE] = next_block;

                    bitmap[inode.indirect] = 1;
                    bitmap[next_block] = 1;
                    disk->read(block.pointers[num_block-POINTERS_PER_INODE], block.data);
                }
            } else {
                next_block = next_free_block();
                if (next_block == 0) {
                    return -1;
                };
                inode.indirect = next_block;
                next_block = next_free_block();

                if (next_block == 0) {
                    return -1;
                };
                disk->read(inode.indirect, block.data);
                
                block.pointers[0] = next_block;

                bitmap[inode.indirect] = 1;
                bitmap[next_block] = 1;
                disk->read(next_block, block.data);
            }


        } else {
            num_block++;
            if (inode.direct[num_block] != 0) {
                next_block = inode.direct[num_block];
                disk->read(inode.direct[num_block], block.data);
            } else {
                next_block = next_free_block();
                if (next_block == 0) {
                    return -1;
                };
                bitmap[next_block] = 1;
                inode.direct[num_block] = next_block;
                disk->read(next_block, block.data);
            }
        }
    cout << next_block << endl;
    int i;
    // Escrita sequencial do inodo no char*data recebido
    for (i = 0; i < length; i++) {

        //Semelhante à verificação inicial, apenas alterando o número do próximo bloco
        if (pos_in_block == Disk::DISK_BLOCK_SIZE) {
            disk->write(next_block, block.data);
            if (num_block + 1 >= POINTERS_PER_INODE) {
                if (inode.indirect != 0) {
                    num_block++;
                    if (num_block-POINTERS_PER_INODE > INODES_PER_BLOCK) {
                        break;
                    }
                    disk->read(inode.indirect, block.data);
                    if (block.pointers[num_block-POINTERS_PER_INODE] != 0) {
                        next_block = block.pointers[num_block-POINTERS_PER_INODE];
                        disk->read(block.pointers[num_block-POINTERS_PER_INODE], block.data);
                    } else {
                        next_block = next_free_block();
                        if (next_block == 0) {
                            break;
                        };

                        block.pointers[num_block-POINTERS_PER_INODE] = next_block;

                        bitmap[inode.indirect] = 1;
                        bitmap[next_block] = 1;
                        disk->read(block.pointers[num_block-POINTERS_PER_INODE], block.data);
                    }
                } else {
                    next_block = next_free_block();
                    if (next_block == 0) {
                        break;
                    };
                    inode.indirect = next_block;
                    next_block = next_free_block();

                    if (next_block == 0) {
                        break;
                    };
                    disk->read(inode.indirect, block.data);
                    
                    block.pointers[0] = next_block;

                    bitmap[inode.indirect] = 1;
                    bitmap[next_block] = 1;
                    disk->read(next_block, block.data);
                }


            } else {
                num_block++;
                if (inode.direct[num_block] != 0) {
                    next_block = inode.direct[num_block];
                    disk->read(inode.direct[num_block], block.data);
                } else {
                    next_block = next_free_block();
                    if (next_block == 0) {
                        inode_save(inumber, &inode);
                        return i;
                    };
                    bitmap[next_block] = 1;
                    inode.direct[num_block] = next_block;
                    disk->read(next_block, block.data);
                }
            }
            pos_in_block = 0;
        }

        block.data[pos_in_block] = data[i];
        pos_in_block++;

    }
    if (next_block != 0) {
        disk->write(next_block, block.data);
    }
    if (offset + i > inode.size) {
        inode.size = offset + i;
    }
    inode_save(inumber, &inode);
    return i;
}

int INE5412_FS::next_free_block() {
    for (size_t num_block = 1; num_block < bitmap.size(); num_block++) {
        if (bitmap[num_block] == 0) {
            bitmap[num_block] = 1;
            return num_block;
        }
    }
    return 0;
}

int INE5412_FS::inode_load(int inumber, fs_inode *inode) {

    union fs_block block;

    inumber--;

    disk->read(0, block.data);
    if (inumber < 0 || block.super.ninodes < inumber) {
        return 0;
    }

    // Pega o bloco no disco referente ao inumber (+ 1 porque 0 é o superbloco)
    int inode_block = inumber / INODES_PER_BLOCK + 1;

    // Lê o inode block e pega o inode relativo ao bloco
    disk->read(inode_block, block.data);
    *inode = block.inode[inumber % INODES_PER_BLOCK];
    return 1;
}

int INE5412_FS::inode_save(int inumber, fs_inode *inode) {

    union fs_block block;

    inumber--;

    disk->read(0, block.data);
    if (inumber < 0 || block.super.ninodes < inumber) {
        return 0;
    }

    // Pega o bloco no disco referente ao inumber (+ 1 porque 0 é o superbloco)
    int inode_block = inumber / INODES_PER_BLOCK + 1;

    // Lê o inode block e pega o inode relativo ao bloco
    disk->read(inode_block, block.data);
    block.inode[inumber % INODES_PER_BLOCK] = *inode;
    disk->write(inode_block, block.data);
    return 1;
}

int INE5412_FS::transition(fs_inode *inode, int &pont, int &block_pos) {
    block_pos++;
    union fs_block block;
    int next_block;
    if (block_pos >= Disk::DISK_BLOCK_SIZE) {
        pont++;
        block_pos = 0;

        //Verifica se excedeu os ponteiros direto
        if (pont >= POINTERS_PER_INODE) {
            if (inode->indirect == 0) {
                next_block = next_free_block();
                if (next_block == 0) {
                    return 0;
                } else {
                    inode->indirect = next_block;
                }
                
            }
            disk->read(inode->indirect, block.data);
            if (block.pointers[pont - POINTERS_PER_INODE] == 0) {
                next_block = next_free_block();
                if (next_block == 0) {
                    return 0;
                } else {
                    block.pointers[pont - POINTERS_PER_INODE] = next_block;
                    disk->write(inode->indirect, block.data);
                }
                
            }
        } else {
            if (inode->direct[pont] == 0) {
                next_block = next_free_block();
                if (next_block == 0) {
                    return 0;
                } else {
                    inode->direct[pont] = next_block;
                }
            }
        }
    }
    return 1;
}
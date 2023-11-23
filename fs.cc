#include "fs.h"

int INE5412_FS::fs_format() {
    if (is_mounted) return 0;

    int nblocks = disk->size();
    int ninodeblocks;

    // Verifica se precisa arredondar pra cima
    if (nblocks % 10) {
        ninodeblocks = nblocks / 10 + 1;
    } else {
        ninodeblocks = nblocks / 10;
    }

    union fs_block block;

    // Como todos os inode_blocks iniciam inválidos, só configura uma vez
    for (int i = 0; i < INODES_PER_BLOCK; i++) {
        block.inode[i].isvalid = 0;
    }

    // Escreve o bloco de inodos configurado em todos os inode_blocks
    for (int i = 0; i < ninodeblocks; i++) {
        disk->write(i+1, block.data);
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
    
    // Sistema de arquivos presente é inválido
    if (block.super.magic != FS_MAGIC) {
        return 0;
    }

    // Inicia considerando todos livres  
    for (int i = 0; i < block.super.nblocks; i++) {
        bitmap.push_back(0);
    }

    bitmap[0] = 1;

    int ninodeblocks = block.super.ninodeblocks;


    for (int i = 0; i < ninodeblocks; i++) {
        disk->read(i+1, block.data);
        bitmap[i+1] = 1; // Blocos de inodo são sempre ocupados

        for (int j = 0; j < INODES_PER_BLOCK; j++) {
            fs_inode inode = block.inode[j];

            if (inode.isvalid) {
                // Verifica blocos diretos
                for (int k = 0; k < POINTERS_PER_INODE; k++) {
                    if (inode.direct[k] != 0) bitmap[inode.direct[k]] = 1;
                }

                // Verifica bloco indireto e blocos de dados associados
                if (inode.indirect != 0) {
                    bitmap[inode.indirect] = 1;

                    union fs_block indirect;
                    disk->read(inode.indirect, indirect.data);

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

    // Busca primeiro inodo disponível
    for (int inode_block = 0; inode_block < ninodeblocks; inode_block++) {
        disk->read(inode_block + 1, block.data);
        for (int inode = 0; inode < INODES_PER_BLOCK; inode++) {
            // Encontrou inodo, configura para o estado inicial (comprimento 0 e ponteiros zerados)
            if (not block.inode[inode].isvalid) {
                block.inode[inode].isvalid = 1;
                block.inode[inode].size = 0;
                for (int drct_point = 0; drct_point < POINTERS_PER_INODE; drct_point++) {
                    block.inode[inode].direct[drct_point] = 0;
                }
                block.inode[inode].indirect = 0;
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

    // Limpa do bitmap blocos diretos
	for (int i = 0; i < POINTERS_PER_INODE; i++) {
		if (inode.direct[i] != 0) {
			bitmap[inode.direct[i]] = 0;
		}
	}
    union fs_block block;

    // Limpa do bitmap o bloco indireto e blocos de dados associados
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
    
    if (not is_mounted) return 0;

    fs_inode inode;
    
    // Se não conseguiu carregar o inode, não é possível ler o arquivo
    if (not inode_load(inumber, &inode)) {
        return 0;
    }

    // Se o inode não é válido, não há arquivo a ser lido
    if (not inode.isvalid) {
        return 0;
    }

    // Se o offset é maior que o tamanho do inodo (em bytes), quer dizer que está buscando uma posição inválida
    if(offset >= inode.size) {
        return 0;
    }

    int num_block = offset/Disk::DISK_BLOCK_SIZE; //Bloco inicial relativo ao inodo
    int pos_in_block = offset % Disk::DISK_BLOCK_SIZE; //Posicao inicial no bloco inicial 

    union fs_block block;

    // Leitura inicial do bloco
    inode_read_block(&inode, num_block, block);

    // Escrita sequencial do inodo no char*data recebido
    for (int i = 0; i < length; i++) {

        // Se durante a leitura de um bloco, chegar no fim do arquivo, volta a quantidade lida até o momento
        if (offset + i + 1>= inode.size) {
            return i + 1;
        }

        // Se chegou no fim do bloco, lê o próximo a partir da posição inicial
        if (pos_in_block == Disk::DISK_BLOCK_SIZE) {
            num_block++;
            inode_read_block(&inode, num_block, block);
            pos_in_block = 0;
        }
        data[i] = block.data[pos_in_block];
        pos_in_block++;
    }

    return length;
}

int INE5412_FS::fs_write(int inumber, const char *data, int length, int offset) {
    
    if (not is_mounted) return 0;

    fs_inode inode;
    
    // Se não conseguiu carregar o inode, não é possível ler o arquivo
    if (not inode_load(inumber, &inode)) {
        return 0;
    }

    // Se o inode não é válido, não há arquivo a ser lido
    if (not inode.isvalid) {
        return 0;
    }

    // Pega a última posição do bloco anterior
    int num_block = offset/Disk::DISK_BLOCK_SIZE - 1;
    int pos_in_block = Disk::DISK_BLOCK_SIZE;

    // Em transition será tualizado para o bloco inicial e verificará a necessidade de alocação
    // Se não conseguiu alocar, nem começa copiar
    if (not transition(&inode, num_block, pos_in_block)) {
        return 0;
    }

    pos_in_block = offset % Disk::DISK_BLOCK_SIZE; //Atualiza a posição inicial

    union fs_block block;

    // Leitura inicial do bloco
    inode_read_block(&inode, num_block, block);

    int i;
    int temp = num_block;

    for (i = 0; i < length; i++) {
        block.data[pos_in_block] = data[i]; // Atualiza o valor no bloco de dados
        temp = num_block; // Salva o antigo num do bloco relativo ao inodo

        // Se não conseguiu alocar um novo bloco, para de copiar
        if (not transition(&inode, num_block, pos_in_block)) {
            inode_write_block(&inode, temp, block);
            break;
        };
        if (num_block != temp) {
            inode_write_block(&inode, temp, block);
            inode_read_block(&inode, num_block, block);
        }
    }
    if (offset + i > inode.size) {
        inode.size = offset + i;
    }
    inode_save(inumber, &inode);
    return i;
}

// Busca o próximo bloco livre a partir do bitmap (retorna o número do bloco no disco ou 0 se não houver)
int INE5412_FS::next_free_block() {
    for (size_t num_block = 1; num_block < bitmap.size(); num_block++) {
        if (bitmap[num_block] == 0) {
            bitmap[num_block] = 1;
            return num_block;
        }
    }
    return 0;
}

// Carrega o inodo do inumber referente
int INE5412_FS::inode_load(int inumber, fs_inode *inode) {

    union fs_block block;

    inumber--; // Ajuste visto que inumber 0 não é válido ao usuário, mas pro disco sim

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

// Salva o inodo no inumber referente
int INE5412_FS::inode_save(int inumber, fs_inode *inode) {

    union fs_block block;

    inumber--; // Ajuste visto que inumber 0 não é válido ao usuário, mas pro disco sim

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

// Escrita de um bloco relativo (pont) ao inode
void INE5412_FS::inode_write_block(fs_inode *inode, int &pont, union fs_block &block) {
    union fs_block block2;

    // Verifica se é relativo a um indireto, se não é um dos bloco direto
    if (pont >= POINTERS_PER_INODE) {
        disk->read(inode->indirect, block2.data);
        disk->write(block2.pointers[pont-POINTERS_PER_INODE], block.data);
    } else {
        disk->write(inode->direct[pont], block.data);
    }
}

// Leitura de um bloco relativo (pont) ao inode
void INE5412_FS::inode_read_block(fs_inode *inode, int &pont, union fs_block &block) {
    union fs_block block2;

    // Verifica se é relativo a um indireto, se não é um dos bloco direto
    if (pont >= POINTERS_PER_INODE) {
        disk->read(inode->indirect, block2.data);
        disk->read(block2.pointers[pont-POINTERS_PER_INODE], block.data);
    } else {
        disk->read(inode->direct[pont], block.data);
    }
}

// Atualiza a posição no bloco a ser lido e verifica necessidade de alocação
int INE5412_FS::transition(fs_inode *inode, int &pont, int &block_pos) {
    block_pos++;
    union fs_block block;
    int next_block;

    if (block_pos >= Disk::DISK_BLOCK_SIZE) {
        pont++;
        block_pos = 0;
    
        //Verifica se excedeu os ponteiros diretos
        if (pont >= POINTERS_PER_INODE) {

            // Aloca um bloco para ponteiros indiretos se não tiver
            if (inode->indirect == 0) {
                next_block = next_free_block();
                // Se next_block = 0, significa que não tem bloco livre
                if (next_block == 0) {
                    return 0;
                } else {
                    inode->indirect = next_block;
                    disk->read(inode->indirect, block.data);
                    // Ajuste para corrigir ponteiros de dados indevidos
                    for (int pointer = 0; pointer < POINTERS_PER_BLOCK; pointer++) {
                        block.pointers[pointer] = 0;
                    }
                    disk->write(inode->indirect, block.data);
                }
                
            }
            disk->read(inode->indirect, block.data);

            // Aloca um bloco de dados se não tiver
            if (block.pointers[pont - POINTERS_PER_INODE] == 0) {
                next_block = next_free_block();
                // Se next_block = 0, significa que não tem bloco livre
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
                // Se next_block = 0, significa que não tem bloco livre
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
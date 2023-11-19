#include "fs.h"
#include <vector>

int INE5412_FS::fs_format()
{
	//Verifica se o disco já foi montado, se sim, não formata.
	//Número de blocos para inode = número de blocos do disco/10
	//Reseta os inodes existentes
	//Atualiza o superbloco

	return 0;
}

void INE5412_FS::fs_debug()
{
	union fs_block block;

	disk->read(0, block.data);

	cout << "superblock:\n";
	cout << "    " << (block.super.magic == FS_MAGIC ? "magic number is valid\n" : "magic number is invalid!\n");
 	cout << "    " << block.super.nblocks << " blocks\n";
	cout << "    " << block.super.ninodeblocks << " inode blocks\n";
	cout << "    " << block.super.ninodes << " inodes\n";


	for (int i = 0; i < block.super.ninodeblocks + 1; i++) {
		disk->read(i+1, block.data);

		for (int j = 0; j < INODES_PER_BLOCK; j++) {
			fs_inode inode = block.inode[j];

			if (inode.isvalid) {
				cout << "inode " << i*INODES_PER_BLOCK + j << ":" << endl;
				cout << "    " << "size: " << inode.size << " bytes" << endl;
				cout << "    " << "direct blocks: ";
				for (int k = 0; k < POINTERS_PER_INODE; k++) {
					if (inode.direct[k] != 0) cout << inode.direct[k] << " "; 
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

int INE5412_FS::fs_mount()
{
	// Construir o bitmap: cria uma lista de booleanos e verifica quais blocos estão livres
	// O bloco zero até ninodeblocks - 1 são restritos, logo nunca serão livres.
	// Demais blocos analisar pelos inodes válidos (blocos diretos, bloco indireto e blocos de dados indiretos não são livres)
	// se der certo o atributo is_mounted fica true, se não fica false
	return 0;
}

int INE5412_FS::fs_create()
{
	// Verifica se há inodos livres (algum que is_valid == false)
	return 0;
}

int INE5412_FS::fs_delete(int inumber)
{
	// Verifica se o inodos é válido, ou seja, está atribuído a algum arquivo
	return 0;
}

int INE5412_FS::fs_getsize(int inumber)
{	

	// Verificar se o disk está montado antes
	union fs_block block;
	
	//Até o isvalid pode ser abstraído numa função que apenas carrega o inode, visto que isso será feito para diversos métodos

	// Talvez essa verificação pode ser feita sem ler o superbloco novamente, adicionando algum atributo a mais no sistema de arquivo
	disk->read(0, block.data);
	if (block.super.ninodes <= inumber) {
		return -1;
	}

	// Pega o bloco no disco referente ao inumber
	int inode_block = inumber/INODES_PER_BLOCK + 1;

	// Lê o inode block e pega o inode relativo ao bloco
	disk->read(inode_block, block.data);
	fs_inode inode = block.inode[inumber % INODES_PER_BLOCK];

	// Se o inode é válido, retorna seu tamanho
	if (inode.isvalid) {
		return inode.size;
	}
	return -1;
}

int INE5412_FS::fs_read(int inumber, char *data, int length, int offset)
{
	return 0;
}

int INE5412_FS::fs_write(int inumber, const char *data, int length, int offset)
{
	//Inodo size igual à soma de todos os blocos relacionados a ele?
	return 0;

}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * @brief Rotina que realiza a troca de conteudo entre as
 * 		 areas da Flash: Run e Storage.
 *
 *       Transfere o conteudo e calcula o CRC do conteudo
 * 		 copiado.
 * 		 Compara conteudo copiado com CRC esperado.
 * 		 Se CRC OK, goto FW_Run.
 * 		 Se CRC Nao_OK, retry MAX_RETRIES_BOOT vezes.
 * 		 Se retries > MAX_RETRIES_BOOT vezes => goto "Panic!"
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
void runBootloader(void)
{
	uint8_t retries = 0;
	uint32_t *p_ini;
	uint32_t *p_ini_crc_calc = NULL;
	uint32_t *p_crc = NULL;
	uint32_t crc_calc = 0;
	uint32_t fw_len = 0;
	uint32_t address_to_save = 0;
	uint32_t *p_fim = NULL;
	uint32_t y = 0;
	_BL_status_t ret = 1;	// ==1 = Erro nao existente

	retry:
		p_ini = _flash_read(FW_RUN_ADDR_INIT);
		p_fim = _flash_read(FW_STORAGE_ADDR_FIM);
		p_crc = _flash_read(FW_STORAGE_ADDR_CRC);
		p_ini_crc_calc = _flash_read(FW_RUN_ADDR_SIGNATURE);

		fw_len = (uint32_t)(*p_fim - FW_RUN_COMPILER_ADDR) - sizeof(unsigned int);		//o campo aponta para a ultima posicao do FW, entao subtrai um long word para compensar a posicao que traz o crc

		while (fw_len % 4 != 0)
		{
		   fw_len++;
		}

		FLASH_GetDefaultConfig(&flash_config_atual);
		flash_config_atual.smartWriteEnable = true;
		while (FLASH_Init(&flash_config_atual) != kStatus_FLASH_Success){};
		while (FLASH_StatusCheck() != kStatus_FLASH_Success){};

		for (y = 0; y <= fw_len; y++)
		{
			address_to_save = FW_RUN_ADDR_INIT+(sizeof(unsigned int)*y);		//Seta endereco de escrita na Flash (Run)
			if (( address_to_save & (FSL_FEATURE_FLASH_PAGE_SIZE_BYTES-1) ) == 0)
			{
				if ((FLASH_Erase (&flash_config_atual, address_to_save, FSL_FEATURE_FLASH_PAGE_SIZE_BYTES) ) != kStatus_FLASH_Success)	//Apaga pagina a ser escrita
				{
					if(retries < MAX_RETRIES_BOOT)
					{
						retries++;
						goto retry;
					}
					else
					{
						retries = 0;
						ret = BL_Copy_Error;
						panic_pisca_leds_fast(ret);
						return;
					}
				}
			}
			retries = 0;
			p_ini = (uint32_t *)(_flash_read(FW_STORAGE_ADDR_INIT+(sizeof(unsigned int)*y)));	//Pega valor na memoria do FW Storage
			if(FLASH_Program(&flash_config_atual, address_to_save, (uint32_t *)&p_ini[0], (uint32_t)sizeof(uint32_t)) != kStatus_FLASH_Success)	//Copia para FW Run
			{
				if(retries < MAX_RETRIES_BOOT)
				{
					retries++;
					goto retry;
				}
				else
				{
					retries = 0;
					ret = BL_Copy_Error;
					panic_pisca_leds_fast(ret);
					return;
				}
			}
		}

		CRC_Reset(base);
		CRC_GetDefaultConfig((crc_config_t *)&crc_config_atual);
		CRC_GetConfig(base, (crc_config_t *)&crc_config_atual);
		crc_config_atual.polynomial = kCRC_Polynomial_CRC_32;
		crc_config_atual.reverseIn = false;
		crc_config_atual.reverseOut = false;
		crc_config_atual.complementIn = false;
		crc_config_atual.complementOut = false;
		crc_config_atual.seed = 0xFFFFFFFFU;
		CRC_Init(base, (crc_config_t *)&crc_config_atual);
		base->MODE = 0x36;

		CRC_WriteData(base, (const uint8_t *)p_ini_crc_calc, (size_t)((uint32_t)(fw_len))) ;
		crc_calc = CRC_Get32bitResult(base);

		if (crc_calc == *p_crc)			//Conteudo copiado Validado OK
		{
			ret = BL_Status_Success;
			if ((FLASH_Erase(&flash_config_atual, (uint32_t)(FW_STORAGE_ADDR_INIT), FSL_FEATURE_FLASH_PAGE_SIZE_BYTES)) != kStatus_FLASH_Success){};		//apaga primeira pagina do fw_storage
			runFirmware();				//Goto Aplication
		}
		else
		{
			if(retries < MAX_RETRIES_BOOT)
			{
				retries++;
				goto retry;
			}
			else
			{
				retries = 0;
				ret = BL_Copy_Error;
				panic_pisca_leds_fast(ret);		// *** Panic !!! ***
			}
		}
}


/** * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * @brief Realiza leitura da Flash a partir de determinado endereco.
 *
 * @param addr Endereco de início de leitura da Flash.
 *
 * @param buf_read Ponteiro para buffer a ser preenchido.
 *
 * @param buf_length Quantidade de posições na Flash que se deseja ler || Tamanho do buffer.
 */
void _flash_read_buf(unsigned int addr, unsigned int *buf_read, size_t buf_length)
{
	for (int x = 0; x < buf_length; x++)
	{
		buf_read[x] = (addr+(x*sizeof(unsigned int)));
	}
}
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
* @brief Rotina de escrita na Flash.
*
* @param data_buf Ponteiro para buffer que contem os dados que se deseja
* 				  gravar na Flash.
*
* @param start_addr Endereco de inicio de escrita na Flash
*
* @param lengthbytes Tamanho em bytes do buffer a ser escrito.
*
* @return status_t true Se conseguir escrever.
*
* @return status_t false Se não conseguir escrever.
*
* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
status_t _flash_write(uint32_t *data_buf, uint32_t start_addr, uint32_t lengthbytes)
{
	FLASH_GetDefaultConfig(&flash_config_atual);
	while (FLASH_Init(&flash_config_atual) != kStatus_FLASH_Success){};

	while (FLASH_StatusCheck() != kStatus_FLASH_Success){};

	if (!FLASH_Erase(&flash_config_atual, start_addr, lengthbytes)){};		//apaga uma pagina
	flash_config_atual.smartWriteEnable = true;  // Enable After every word write operation, flash controller will read back the value from the address it just wrote to and check whether it is same as the value written
	if (FLASH_Program(&flash_config_atual, start_addr, (uint32_t *)&data_buf[0], lengthbytes))	//ESCREVE UMA PAGINA
	{
		return kStatus_FLASH_Fail;
	}
	else
	{
		return kStatus_FLASH_Success;
	}
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
* @brief Rotina para salvar dados na Flash.
*
* @param data_buf Ponteiro para buffer que contem os dados.
*
* @param start_addr Endereco inicial que se deseja gravar.
*
* @param lengthbytes Tamanho em bytes do buffer a ser gravado
*
* @return status_t true Dados salvos na Flash
*
* @return status_t false Dados nao salvos
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
status_t teste_storage(uint32_t *data_buf, uint32_t start_addr, uint32_t lengthbytes)
{
	status_t ret;

	ret = _flash_write((uint32_t *)&data_buf[0], start_addr, lengthbytes);

	if (ret == kStatus_FLASH_Fail)
	{
		return kStatus_FLASH_Fail;
	}
	else if (ret == kStatus_FLASH_Success)
	{
		return kStatus_FLASH_Success;
	}
	else
	{
		return ret;
	}
}


/**
* @brief Rotina de configuracao para Calculo do CRC 32.
*
* @param _base Ponteiro para Struct do Registrador do CRC.
*
* @param _crc_config_atual Ponteiro para Struct de configuracao do CRC.
*/
void _crc32_config(CRC_Type *_base, crc_config_t *_crc_config_atual)
{
	CRC_Reset(_base);
	CRC_GetDefaultConfig((crc_config_t *)&_crc_config_atual);
	CRC_GetConfig(_base, (crc_config_t *)&_crc_config_atual);
	crc_config_atual.polynomial = kCRC_Polynomial_CRC_32;
	crc_config_atual.reverseIn = false;
	crc_config_atual.reverseOut = false;
	crc_config_atual.complementIn = false;
	crc_config_atual.complementOut = false;
	crc_config_atual.seed = 0xFFFFFFFFU;
	CRC_Init(_base, (crc_config_t *)&_crc_config_atual);
	_base->MODE = 0x36;		// hard-coded
}



/**
* @brief Rotina que realiza a troca de conteudo entre as
* 		 areas da Flash: Run e Storage.
*/
void runBootloader(void)
{
	uint32_t *p_ini;
	uint32_t *p_crc;
	uint32_t crc_calc = 0;
	uint32_t fw_len = 0;
	uint32_t address_to_save = 0;
	int *p_fim = NULL;
	int aux = 0;

	p_crc = (uint32_t *)(_flash_read(FW_STORAGE_ADDR_CRC));
	p_ini = (uint32_t *)(_flash_read(FW_STORAGE_ADDR_INIT));
	p_fim = (int *)(_flash_read(FW_STORAGE_ADDR_FIM));		// endereço do fim do fw_storage armazenado

	//If enviar endereço final ->
	fw_len = (uint32_t)(*p_fim - 0x8000);
	while (fw_len % 4 != 0)
	{
	   fw_len--;
	}

	CRC_Reset(base);
	CRC_GetDefaultConfig((crc_config_t *)&crc_config_atual);
	CRC_GetConfig(base, (crc_config_t *)&crc_config_atual);
	crc_config_atual.polynomial = kCRC_Polynomial_CRC_32;
	crc_config_atual.reverseIn = false;
	crc_config_atual.reverseOut = false;
	crc_config_atual.complementIn = false;
	crc_config_atual.complementOut = false;
	crc_config_atual.seed = 0xFFFFFFFFU;
	CRC_Init(base, (crc_config_t *)&crc_config_atual);
	base->MODE = 0x36;		// hard-coded 

	CRC_WriteData(base, (const uint8_t *)p_fim, (size_t)((uint32_t)(fw_len-8))) ;
	crc_calc = CRC_Get32bitResult(base);

	if (crc_calc == *p_crc)			//Deve ser "=="
	{
		//crc de firmware storage valido
		flash_config_atual.smartWriteEnable = true;
		if (( FLASH_Erase ( &flash_config_atual, (uint32_t)(FW_RUN_ADDR_INIT), FSL_FEATURE_FLASH_PAGE_SIZE_BYTES ) ) != kStatus_FLASH_Success){};		//apaga primeira pagina
		for (uint32_t y=0; y <= fw_len; y++)
		{
			aux = (sizeof(unsigned int)*y);  //0x04 ; 0x08 ; 0x12 ; 0x16 ; 0x20... 0xF8000
			address_to_save = FW_RUN_ADDR_INIT+aux; // 0x21008118 ; 0x2100811C ; 0x21008120 ; ... ; 0x21046114
			if (( address_to_save & (FSL_FEATURE_FLASH_PAGE_SIZE_BYTES-1) ) == 0)
			{
				if (( FLASH_Erase ( &flash_config_atual, (uint32_t)(address_to_save), FSL_FEATURE_FLASH_PAGE_SIZE_BYTES) ) != kStatus_FLASH_Success){};		//apaga uma pagina
			}
			p_ini = (uint32_t *)(_flash_read(FW_STORAGE_ADDR_INIT+aux));	//PEGA VALOR NA MEMORIA DO FW_STORAGE
			while ( (FLASH_Program(&flash_config_atual, address_to_save, (uint32_t *)&p_ini[0], (uint32_t)0x04)) != kStatus_FLASH_Success ){};	//copia para fw_run
		}
		//antes de apagar fazer CRC novamente -> mas agora do fw na area de RUN que foi copiado
		//se
		if ((FLASH_Erase(&flash_config_atual, (uint32_t)(FW_STORAGE_ADDR_SIGNATURE), FSL_FEATURE_FLASH_PAGE_SIZE_BYTES)) != kStatus_FLASH_Success){};		//apaga primeira pagina do fw_storage

		runFirmware();
	}
	else
	{
		runFirmware();
	}
}

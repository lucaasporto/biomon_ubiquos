# BioMon — Sistema de Aquisição de Baixas Tensões para Monitoramento de Células Bioeletroquímicas

Sistema embarcado de baixo custo, baseado em ESP32, para leitura simultânea (via multiplexação) de até 12 canais de baixa tensão (escala de milivolts), voltado à medição de potencial elétrico de células bioquímicas/bioeletroquímicas (ex.: células de combustível microbianas — MFCs). O sistema realiza leitura periódica dos canais, disponibiliza um dashboard web local em tempo real e registra os dados automaticamente em uma planilha do Google Sheets.

> **Autores do projeto:**
> - Nome do(s) autor(es) — papel no projeto (ex.: firmware, hardware, testes)
> - *(preencha aqui os nomes e papéis de cada integrante)*

---

## Sumário

- [Introdução](#introdução)
- [Hardware Necessário](#hardware-necessário-material-utilizado)
- [Esquema de Conexão](#esquema-de-conexão)
- [Instalação](#instalação)
  - [Hardware](#hardware-1)
  - [Software](#software)
- [Projeto Final](#projeto-final)
- [Tutorial](#tutorial)
- [Referências](#referências)

---

## Introdução

Células bioeletroquímicas — como as células de combustível microbianas (*microbial fuel cells*, MFCs) — geram tensões elétricas relativamente baixas (tipicamente entre poucos milivolts e cerca de 800 mV), que precisam ser monitoradas ao longo do tempo, em intervalos regulares, para caracterizar seu desempenho eletroquímico (tensão em circuito aberto, curva de polarização, densidade de potência, resistência interna, entre outros parâmetros).

Esse tipo de monitoramento contínuo é tradicionalmente feito com data loggers comerciais ou multímetros de bancada. Ambas as soluções apresentam limitações relevantes para laboratórios com orçamento reduzido: data loggers de precisão têm custo elevado, e multímetros digitais de baixo custo normalmente possuem um único canal e exigem coleta manual dos dados, o que inviabiliza o monitoramento simultâneo de múltiplos reatores por longos períodos.

Como alternativa, diferentes trabalhos publicados na literatura já validaram o uso de microcontroladores de baixo custo (como Arduino UNO e ESP32) associados a conversores analógico-digitais externos de maior resolução (ex.: ADS1115) como sistemas de aquisição de dados para monitoramento eletroquímico de MFCs. Esses estudos compararam estatisticamente as leituras dos microcontroladores com as de multímetros de referência e não encontraram diferença significativa entre eles, obtendo erros médios absolutos e relativos da ordem de ~1 mV e ~1-3%, o que indica que essa abordagem é uma alternativa viável e precisa para substituir instrumentação cara em estudos bioeletroquímicos.

Este projeto segue a mesma motivação: oferecer um sistema de aquisição de dados **preciso, expansível e de baixo custo** para leitura de baixas tensões geradas por células bioquímicas. A diferença deste sistema em relação aos trabalhos citados está na arquitetura de expansão de canais: em vez de utilizar múltiplas entradas analógicas fixas, o projeto combina um único ADS1115 com dois multiplexadores analógicos CD74HC4067, permitindo escalar a leitura para até 16 canais (12 em uso na versão atual) endereçados sequencialmente por um único conversor A/D, além de oferecer configuração e visualização via interface web embarcada, sem a necessidade de conexão física a um computador durante a operação.

---

## Hardware Necessário (Material Utilizado)

| Componente | Descrição e especificações |
|---|---|
| **ESP32 DEVKIT V1** | Microcontrolador principal do sistema. Responsável pela leitura dos sensores, hospedagem do dashboard web (Access Point + modo estação Wi-Fi), sincronização de horário via NTP e envio dos dados ao Google Sheets. |
| **ADS1115** | Conversor analógico-digital (ADC) externo de 16 bits, comunicação via I2C. Utilizado para leitura de tensão com maior resolução que o ADC interno de 12 bits do ESP32. No firmware atual, o ganho é configurado como 4x (faixa de ±1,024 V, resolução de ~0,03125 mV por bit), otimizado para a leitura de sinais na faixa de milivolts típica de células bioeletroquímicas. |
| **2× CD74HC4067** | Multiplexadores/demultiplexadores analógicos de 16 canais. Endereçados por um barramento comum de 4 bits de seleção, permitem comutar sequencialmente entre os canais monitorados, roteando o sinal de cada célula até a entrada do ADS1115. Na versão atual do firmware, 12 dos 16 canais disponíveis são utilizados (expansível até 16). |
| Cabos jumper / protoboard ou PCB | Utilizados para as conexões elétricas entre ESP32, ADS1115, multiplexadores e as células sob teste. |
| Fonte de alimentação | Alimentação de 5V/3,3V para o ESP32 e os módulos eletrônicos *(especifique aqui a fonte utilizada em seu protótipo)*. |
| Células bioeletroquímicas sob teste | Reatores/células cujo potencial elétrico será monitorado (até 12 unidades na configuração atual). |

> 📷 **Insira aqui uma foto de todos os componentes de hardware separados, antes da montagem.**
>
> ![Componentes de hardware do projeto](docs/imagens/componentes_hardware.png)

---

## Esquema de Conexão

O barramento de seleção de canal (4 bits, comum aos dois multiplexadores CD74HC4067) é conectado aos seguintes pinos do ESP32:

| Sinal | Pino do ESP32 | Função |
|---|---|---|
| S0 | GPIO 32 | Bit menos significativo (LSB) do endereço do canal |
| S1 | GPIO 33 | Bit 1 do endereço do canal |
| S2 | GPIO 25 | Bit 2 do endereço do canal |
| S3 | GPIO 26 | Bit mais significativo (MSB) do endereço do canal |
| SDA (I2C) | GPIO 21 (padrão ESP32) | Comunicação com o ADS1115 |
| SCL (I2C) | GPIO 22 (padrão ESP32) | Comunicação com o ADS1115 |

Como os dois multiplexadores compartilham o mesmo barramento de seleção (S0–S3), ambos comutam de forma sincronizada para o mesmo índice de canal a cada leitura. Essa arquitetura permite, por exemplo, rotear os dois terminais (positivo e referência) de cada célula até as entradas do ADS1115 a cada ciclo de leitura, evitando que todas as células monitoradas compartilhem um único referencial de terra. **Recomenda-se validar e detalhar essa ligação de acordo com o esquema elétrico específico do seu protótipo**, incluindo qual terminal de cada célula é conectado a cada multiplexador e qual entrada do ADS1115 é utilizada.

> 📷 **Insira aqui o diagrama elétrico completo (esquemático) do sistema, mostrando ESP32, ADS1115 e os dois CD74HC4067.**
>
> ![Esquema elétrico do sistema](docs/imagens/esquema_eletrico.png)

> 📷 **Insira aqui uma foto do protótipo já montado em protoboard ou PCB, com as ligações visíveis.**
>
> ![Protótipo montado](docs/imagens/prototipo_montado.png)

---

## Instalação

### Hardware

1. Monte o ADS1115 e os dois módulos CD74HC4067 na protoboard/PCB, seguindo o [esquema de conexão](#esquema-de-conexão) acima.
2. Conecte os pinos de seleção (S0–S3) de ambos os multiplexadores em paralelo aos GPIOs 32, 33, 25 e 26 do ESP32.
3. Conecte o ADS1115 ao barramento I2C do ESP32 (SDA/SCL).
4. Conecte os terminais de cada célula bioeletroquímica aos canais correspondentes dos multiplexadores.
5. Alimente o circuito conforme a tensão de operação dos módulos utilizados.

> 📷 **Insira aqui fotos do passo a passo da montagem do hardware (fiação, soldagem, fixação dos módulos, etc.).**
>
> ![Passo a passo da montagem](docs/imagens/montagem_passo_a_passo.png)

### Software

1. **Instale a Arduino IDE** (versão mais recente recomendada).
2. **Adicione o suporte à placa ESP32** em `Arquivo > Preferências > URLs Adicionais para Gerenciadores de Placas`, utilizando a URL do pacote de placas Espressif, e instale o pacote `esp32` pelo Gerenciador de Placas.
3. **Instale as bibliotecas necessárias** pelo Gerenciador de Bibliotecas da Arduino IDE:
   - `Adafruit_ADS1X15` (leitura do ADS1115);
   - `ESP_Google_Sheet_Client` (envio de dados ao Google Sheets);
   - `Preferences` (armazenamento persistente de configurações na memória flash do ESP32 — já incluída no core do ESP32);
   - Biblioteca **Mongoose** (responsável pela interface web embarcada). O arquivo `mongoose_glue.c`/`.h` presente no projeto é **gerado automaticamente pelo [Mongoose Wizard](https://mongoose.ws/wizard/)** e não deve ser editado manualmente — qualquer alteração de layout ou de variáveis do dashboard deve ser feita reexportando o projeto pelo Wizard.
4. **Configure o acesso ao Google Sheets:**
   - Crie um projeto no Google Cloud e uma conta de serviço com acesso à API do Google Sheets.
   - Compartilhe a planilha de destino com o e-mail da conta de serviço.
   - No arquivo principal do firmware, preencha as constantes `PROJECT_ID`, `CLIENT_EMAIL`, `PRIVATE_KEY` e `spreadsheetId` com as credenciais do seu próprio projeto Google Cloud (**nunca reutilize ou publique as credenciais originais do desenvolvimento**).
5. **Compile e grave o firmware** no ESP32 pela Arduino IDE.
6. **Primeiro acesso:** ao ligar, o ESP32 cria automaticamente uma rede Wi-Fi própria (Access Point). Conecte-se a essa rede pelo celular ou computador e acesse a interface web para configurar a rede Wi-Fi definitiva e o intervalo de envio dos dados.

> 📷 **Insira aqui uma captura de tela do Gerenciador de Placas com o ESP32 instalado.**
>
> ![Instalação da placa ESP32 na Arduino IDE](docs/imagens/instalacao_esp32_ide.png)

---

## Projeto Final

O firmware final integra os seguintes módulos e funcionalidades:

- **Leitura multiplexada:** varredura de até 12 canais analógicos (expansível a 16) via ADS1115 + 2× CD74HC4067, com atualização local de até 4 leituras por segundo.
- **Dashboard web embarcado:** interface (gerada via Mongoose Wizard) para visualização em tempo real das tensões de cada canal, resumo estatístico (maior valor, menor valor, média e número de canais ativos) e habilitação/desabilitação individual de cada uma das 12 células monitoradas.
- **Configuração de rede via web:** o ESP32 opera simultaneamente como ponto de acesso próprio (rede local "BioMon") e como estação conectada à rede Wi-Fi do laboratório; as credenciais podem ser alteradas diretamente pela interface web, sem necessidade de regravar o firmware.
- **Sincronização de horário (NTP):** utilizada para registrar o timestamp correto de cada leitura enviada.
- **Registro automático em nuvem:** envio periódico dos dados para uma planilha do Google Sheets, com intervalo de envio configurável (entre 0,2 s e 1800 s) e dois modos de operação — envio direto (intervalos ≥ 1 s) e envio em lote (*batching*, para intervalos menores que 1 s).
- **Persistência de configurações:** rede Wi-Fi e intervalo de log são armazenados na memória flash do ESP32, preservando as configurações após reinicializações ou quedas de energia.
- **Modo econômico:** possibilidade de desligar a leitura/atualização local do dashboard mantendo apenas o envio dos dados para a planilha, reduzindo o consumo de processamento.

> 📷 **Insira aqui capturas de tela do dashboard web em funcionamento (visualização das tensões, tela de configuração de Wi-Fi e tela de intervalo de log).**
>
> ![Dashboard web do sistema](docs/imagens/dashboard_web.png)

O código-fonte principal está organizado da seguinte forma:

- `wizard.ino` — firmware principal (leitura dos sensores, lógica de multiplexação, Wi-Fi, NTP, Google Sheets e registro dos handlers da interface web).
- `mongoose_glue.c` / `mongoose_glue.h` — código gerado automaticamente pelo Mongoose Wizard, faz a ponte entre a interface web e as variáveis do firmware (**não editar manualmente**).

---

## Tutorial

Esta seção reúne os tutoriais utilizados para testar cada módulo do sistema de forma isolada, antes da integração final. Recomenda-se organizar o código de cada teste em uma subpasta própria do repositório, por exemplo:

```
/docs/tutoriais/
  ├── 01_teste_ads1115/       -> leitura básica de um canal do ADS1115
  ├── 02_teste_cd74hc4067/    -> varredura dos canais do multiplexador
  ├── 03_teste_wifi_ap_sta/   -> teste do modo Wi-Fi (Access Point + estação)
  └── 04_teste_google_sheets/ -> teste de envio de dados para o Google Sheets
```

Em cada subpasta, inclua o código de teste utilizado e um pequeno `README.md` descrevendo:

- o objetivo do teste;
- os problemas encontrados durante a instalação das ferramentas de software/hardware ou durante os testes;
- como cada problema foi solucionado.

> 📷 **Insira aqui, se útil, imagens ou capturas de tela dos resultados dos testes individuais de cada sensor/módulo (ex.: leitura no Serial Monitor).**
>
> ![Resultado dos testes individuais](docs/imagens/testes_individuais.png)

---

## Referências

- INDRIYANI, Y. A.; RUSTAMI, E.; RUSMANA, I.; ANWAR, S.; DJAJAKIRANA, G.; SANTOSA, D. A. **Bioelectricity production of microbial fuel cells (MFCs) and the simultaneous monitoring using developed multi-channels Arduino UNO-based data logging system.** Journal of Applied Electrochemistry, v. 54, p. 503–518, 2024.
- INDRIYANI, Y. A.; EFENDI, R.; RUSTAMI, E.; RUSMANA, I.; ANWAR, S.; DJAJAKIRANA, G.; SANTOSA, D. A. **Affordable ESP32-based monitoring system for microbial fuel cells: real-time analysis and performance evaluation.** International Journal of Energy and Water Resources, 2023.

---

*Documentação criada com base no modelo de estrutura básica para README de projetos no GitHub. Complete os campos indicados (autores, imagens, credenciais, esquema elétrico) antes da publicação final.*

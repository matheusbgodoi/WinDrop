# Port do winawdl para a MediaTek MT7902

Status: 2026-11-16. Fases 0 a 7 concluídas no essencial: AirDrop validado nos dois sentidos com o MacBook e com o iPhone, e o aplicativo instalado (WinDrop, ver [WINDROP.md](WINDROP.md)) liga e desliga o rádio sozinho, com botão direito e chave na bandeja. Em aberto: Wi-Fi e AirDrop não funcionam ao mesmo tempo no PC (a placa troca de papel), a velocidade é de 1,1 MB/s e OFDM em 2,4 GHz não sai.

## Registro de resultados

| Data | Fase | Resultado |
|---|---|---|
| 2026-11-16 | 1 | Aprovada. `regproxy` ligado à placa com Secure Boot ligado (certificado local). `probes/mt7902_chipid` leu BAR de 1 MiB em `0xf820300000`, mapeado direto, `LPCTL 0x0` (driver dono do chip) e `chip id 0x7902`, revisão `0x0000` |
| 2026-11-16 | 2 | Aprovada. `mt7902/fwboot` executou reset, anéis WFDMA por polling, patch (1 seção) e RAM (4 regiões, build 20251212032127). `MT_CONN_ON_MISC` passou de `0x0` para `0x3` (firmware pronto). DMA a partir de 512 páginas de 4 KiB abaixo de 4 GiB |
| 2026-11-16 | 3 | Aprovada. `mt7902/rxtest` em modo sniffer: canal 6 com 97 a 180 quadros em 4 a 5 s (beacons, dados, quadros de ação AWDL de aparelhos Apple), canal 44 com 14 quadros, canal 149 com 1583 quadros. Primeiro quadro entre 16 e 266 ms. MAC lido do chip, 1 fluxo espacial, 2,4 e 5 GHz |
| 2026-11-16 | 4 | Parcial. `mt7902/txtest` envia pedidos de sondagem em broadcast. A 1 Mb/s (CCK) no canal 6, 15 a 22 respostas de pontos de acesso vizinhos voltaram endereçadas ao MAC da placa, o que prova a transmissão no ar. A 6 Mb/s (OFDM), nos canais 6 e 157, nenhuma resposta |
| 2026-11-16 | 4 | Aprovada em 5 GHz. Com o MacBook em modo monitor como testemunha, 30 de 30 pedidos de sondagem a 6 Mb/s OFDM no canal 149 foram capturados. Em 2,4 GHz só 1 Mb/s sai no ar |
| 2026-11-16 | 5 | Aprovada. `mt7902/radiod` serve o protocolo `radio/radio_link.h`; o `awdld` do winawdl, sem alteração, roda sobre ele no canal 149. Achou o MacBook e outro aparelho Apple, elegeu mestre de sincronismo, criou a interface `awdl0` (Wintun) e `ping -6` ao endereço link-local do `awdl0` do Mac teve 10 de 10 respostas (6 a 456 ms, média 218 ms) |
| 2026-11-16 | 6 | Parcial. PC para Mac aprovado: `airdrop/sender.py` achou o MacBook, o pedido foi aceito e um arquivo de 20 MB chegou com SHA-256 idêntico, a 1,1 MB/s. O receptor (`airdrop/receiver.py`, "PC do Matheus") aparece no AirDrop do Mac e respondeu a `POST /Discover`. Faltam Mac para PC e os dois sentidos com o iPhone |
| 2026-11-16 | 6 | Mac para PC aprovado: uma imagem de 104425 bytes arrastada no AirDrop do Mac chegou em Downloads pelo aplicativo (`POST /Ask` e `POST /Upload` com 200). Falta o iPhone nos dois sentidos |
| 2026-11-16 | 7 | Parcial. O aplicativo WPF do winawdl, apontado para `radiod`, mostra o MacBook, recebe e segura arquivos. Novos: "Enviar via AirDrop" no botão direito (`app/src/Shell/SendMenu.cs`, registro em HKCU, arquivos repassados à instância aberta por pipe) e a chave "AirDrop" no menu da bandeja. Conferido por captura da janela (`scripts/app-shot.ps1`). O liga/desliga ainda não sobe o rádio sozinho nesta máquina: falta novo logon para o direito "Bloquear páginas na memória" valer |
| 2026-11-16 | 6 | Aprovada. iPhone para PC: uma foto de 105043 bytes recebida pelo aplicativo. PC para iPhone: `sender.py --to iPhone` achou o aparelho, o pedido foi aceito e o arquivo enviado. Para o iPhone foi preciso anunciar um fluxo espacial (`awdld -s 1`) |
| 2026-11-16 | 7 | Cópia instalada em `C:\Program Files\winawdl`. Driver de desenvolvimento e executor SYSTEM removidos; a placa voltou ao driver da MediaTek e o Wi-Fi conectou (canal 157, 802.11ac) depois de as 8 redes salvas serem trazidas da identidade antiga do adaptador. A chave Radio, acionada por automação (`scripts/app-radio-switch.ps1`), pegou a placa, o `radiod` parou com "this account may not lock pages in memory" e a placa foi devolvida (`wifi: back`). Falta o novo logon para o direito valer |
| 2026-11-16 | 7 | Aprovada na cópia instalada. A chave Radio passa a pedir uma tarefa agendada (`winawdl radio`, LocalSystem) em vez de elevação: sem pergunta do Windows e sem depender do direito de bloquear páginas na conta. Ciclo medido: chave ligada, `radiod` e `awdld` de pé, "Visible as PC-MATHEUS" e o MacBook na lista; arquivo entregue ao aplicativo como o botão direito faz e enviado ao Mac (`scripts/app-send.ps1`), conferido em Downloads do Mac; chave desligada, placa devolvida em cerca de 10 s, 8 redes salvas de volta, Wi-Fi conectado e com internet |

Observações da fase 4:

- **Testemunha independente.** `scripts/sniff-test.ps1` roda o `txtest` enquanto o MacBook captura em modo monitor por tempo fixo (`scripts/mac/`, instalado com uma regra de sudoers restrita a um script). Resultados do Mac, a cerca de 1 m:
  - canal 6, 1 Mb/s: 18 quadros da placa, -39 dBm;
  - canal 149, 6 Mb/s OFDM: 30 de 30 quadros, `6.0 Mb/s 5745 MHz 11a`, -46 dBm;
  - canal 6, 6 Mb/s OFDM: nenhum quadro em 1454 recebidos pelo filtro.
- **OFDM funciona em 5 GHz.** A captura acima veio depois de três mudanças: tipo e subtipo do quadro na palavra 7 do TXD, índice de extensão espacial 24 e o passo de `mt792x_mac_init` (`TMAC_CTCR0` e limiar de RTS por `PROTECT_CTRL`). Não foi isolado qual delas importa, e antes não havia testemunha em 5 GHz, então é possível que já funcionasse. A falta de respostas no canal 157 não provava nada.
- **Risco aberto, OFDM em 2,4 GHz não sai no ar.** Mesma montagem, só muda o canal. Suspeita não testada: proteção ERP. Afeta o canal 6 do AWDL, não os canais 44 e 149.
- O relatório de liberação de TX do firmware diz "entregue" para quadros que não saíram (canal 6 OFDM), então não serve de testemunha.
- **O Mac demora a voltar à rede depois da captura.** Em duas das cinco capturas voltou sozinho em menos de 40 s; nas outras ficou minutos sem enlace, mesmo com o ciclo de desligar e ligar o Wi-Fi e com a tela acordada, e foi preciso reconectar à mão. Causa desconhecida. Por isso o farejador deve ser usado o mínimo possível.
- Quadros unicast com ACK pedido ficaram sem notificação de liberação no teste a 1 Mb/s. Não investigado.
- **Comandos em rajada derrubam o firmware.** Vários comandos sem resposta seguidos (a tabela de potência são 8) fazem o firmware pedir parada de DMA, de forma determinística. O mt76 lê um registrador pelo firmware depois de cada um, e o porte faz o mesmo (`mcu_sync`). Isso pode estar ligado ao erro aleatório depois do boot.
- Caminho de TX: anel 0, descritor de 32 bytes mais lista de páginas no buffer do anel, apontando para o quadro no mesmo buffer. O token é o índice do descritor.

Observações da fase 3:

- **Risco aberto, boot instável.** Em cerca de metade dos boots o firmware pede parada de DMA (`MT_MCU_CMD = STOP_DMA`, bit 29 em `HOST_INT_STA`) até 300 ms depois de iniciar e desliga o WFDMA, sem nenhum comando enviado. Não depende de desligamento limpo nem da execução anterior. Causa desconhecida. `mt7902_start` refaz o boot quando isso acontece: 10 de 10 execuções subiram em até 4 tentativas. O protocolo de recuperação (`MT_MCU_INT_EVENT`) existe no hardware e não foi implementado.
- No canal 6 parte dos buffers do anel de dados não tem um descritor de recepção válido no início (57 em 154 numa captura). Hipótese: quadros que ocupam mais de um buffer. Não acontece nos canais de 5 GHz testados. A investigar antes da fase 5.
- O firmware reporta 1 fluxo espacial para esta placa.
- Sequência mínima depois do boot: consulta de capacidades, modo EFUSE, MAC ligado, canal (`SET_RX_PATH`), `DEV_INFO`, `BSS_INFO`, sniffer ligado, configuração do sniffer, `CHANNEL_SWITCH`, filtro de recepção zerado.
- Testes sem um UAC por execução: `scripts/system-runner.ps1 -Install` cria uma tarefa SYSTEM temporária que só executa programas de `mt7902/build`. Remover com `-Remove` ao fim da sessão.

Observações da fase 2:

- O chip só aceita endereços de DMA de 32 bits (o mt76 usa máscara de 32 bits). Nesta máquina não havia blocos de 2 MiB livres abaixo de 4 GiB, então o slab é feito de páginas de 4 KiB. Por isso cada anel tem no máximo 256 descritores (uma página).
- Nenhuma interrupção é habilitada (`HOST_INT_ENA = 0`, `PCIE_MAC_INT_ENABLE = 0`). Comandos e eventos funcionam só com polling de `dma_idx`. O risco 1 está resolvido para o caminho de comando.
- A conta de administrador não tem o direito "Bloquear páginas na memória". Os testes rodam como SYSTEM por `scripts/run-elevated.ps1 -AsSystem`. O app final vai precisar conceder esse direito ou rodar o rádio como serviço.
- O driver repete a parada de DMA quando o handle fecha (`IOCTL_REGPROXY_SET_CLOSE_OPS`), como no projeto original.

Observações da fase 1:

- A placa expõe duas regiões de memória. O driver escolhe a primeira com 1 MiB ou mais.
- O BAR fica acima de 4 GiB, o que não importa para registradores.
- Para voltar ao driver da MediaTek: `scripts/uninstall-driver.ps1` em shell elevado.

## Objetivo

AirDrop real (AWDL) no Windows 11 usando a placa Wi-Fi embutida da ASUS TUF GAMING A620M-PLUS WIFI,
uma MediaTek MT7902 (`PCI\VEN_14C3&DEV_7902&SUBSYS_60401A3B`), sem adaptador USB e sem VM Linux.
Alvos de teste: um MacBook Pro e um iPhone do mesmo dono.

O winawdl original só funciona na Realtek RTL8852CE. Este fork troca a camada de rádio.

## O que se reaproveita e o que precisa ser escrito

| Camada | Origem | Situação para a MT7902 |
|---|---|---|
| Driver UMDF 2 que toma a placa e expõe registradores (`driver/regproxy`) | winawdl | Reaproveitar. Trocar o hardware ID no INF. Verificar qual BAR a MT7902 usa |
| Memória de DMA em modo usuário (AWE, blocos de 2 MiB) | winawdl | Reaproveitar a técnica. O layout dos anéis muda |
| **Driver do chip em modo usuário** | devourer (só Realtek) | **Escrever.** É o trabalho principal deste fork |
| Protocolo rádio/AWDL (`radio/radio_link.h`, pipe nomeado) | winawdl | Reaproveitar sem mudança, é a fronteira do port |
| Daemon AWDL (OWL no Windows, Wintun `awdl0`) | winawdl | Reaproveitar |
| AirDrop (omdrop adaptado), BLE, app | winawdl | Reaproveitar. O Bluetooth da placa é MediaTek USB (`VID_13D3&PID_3596`) e continua com o driver do Windows |

## Referência para o driver do chip

O driver Linux `mt76` passou a suportar a MT7902 pelo módulo `mt7921e` (mainline no Linux 7.1).
Uma cópia de referência fica em `ref/mt76` (não versionada). O firmware vem junto:
`WIFI_MT7902_patch_mcu_1_1_hdr.bin` e `WIFI_RAM_CODE_MT7902_1.bin`.

Tamanho do código de referência relevante: cerca de 17 mil linhas (`mt7921/`, `mt792x_*`, `mt76_connac_*`, `dma.c`).
Só uma fração é necessária para modo monitor com injeção. Diferenças da MT7902 dentro do mt7921e:
layout de anéis próprio (`MT7902_TXQ_MCU_WM`, `MT7902_RX_MCU_RING_SIZE`), sem anel MCU_WA,
máscara de interrupção diferente e gerenciamento de energia desativado.

A MT7902 é um chip dirigido por firmware: quase tudo é feito com comandos MCU, e não com registradores
diretos como na Realtek. Isso muda a natureza do port: menos calibração de rádio à mão, mais dependência
do que o firmware aceita fazer.

## Fases

| Fase | Entrega | Critério de aceite |
|---|---|---|
| 0 | Viabilidade | Este documento |
| 1 | regproxy ligado à MT7902 | Ler `MT_HW_CHIPID` e obter `0x7902` a partir de um programa comum |
| 2 | DMA e boot do firmware | Patch e RAM code carregados, firmware responde a um comando MCU |
| 3 | Recepção em modo monitor | Quadros capturados nos canais 6 e 44 |
| 4 | Transmissão (injeção) | Quadros vistos no ar pelo MacBook em modo sniffer |
| 5 | AWDL | O daemon OWL enxerga o iPhone e o Mac como pares e sincroniza |
| 6 | AirDrop | Receber e enviar um arquivo com iPhone e Mac |
| 7 | Integração no Windows | Menu de contexto, ícone na bandeja com liga e desliga |

## Riscos abertos, na ordem em que aparecem

1. **Interrupções.** O caminho da 8852CE no winawdl faz polling. O mt7921e depende de interrupção. É preciso confirmar que a MT7902 funciona só com polling dos anéis.
2. **Firmware e modo monitor.** No Linux o mt7921 faz monitor e injeção, mas isso não foi confirmado para a MT7902 especificamente. Se o firmware recusar, o projeto para na fase 3 ou 4.
3. **ACK por hardware e tempo de troca de canal.** O AWDL alterna entre os canais 6 e 44 várias vezes por segundo. O winawdl precisou de ACK por hardware para chegar a uma taxa útil. Na MT7902 isso depende de como o firmware trata o endereço próprio em modo monitor.
4. **Assinatura do driver.** O Secure Boot desta máquina ainda não foi lido (precisa de shell elevado). O winawdl usa UMDF com certificado local, que funciona com Secure Boot ligado.
5. **Recuperação.** Se o driver falhar, o Wi-Fi do Windows fica fora até a placa voltar ao driver da MediaTek. A máquina usa cabo, então o impacto é baixo.

## Ambiente desta máquina

- Windows 11 Pro for Workstations, build 26300. Integridade de memória (HVCI) desligada. Virtualização ligada.
- Driver MediaTek 3.4.0.1063 (`oem23.inf`). Rede por cabo (Realtek 2.5GbE), Wi-Fi desconectado.
- Instalado: Build Tools do Visual Studio 2022, CMake, Python 3.12, WSL Ubuntu.
- Falta: Windows Driver Kit, .NET SDK 10, MinGW-w64.
- MacBook acessível por Tailscale em `100.104.208.37`. O acesso por chave SSH ainda não está liberado.

## Licenças

O código novo do rádio deriva do mt76 (BSD-3-Clause-Clear) e não do devourer (GPL-2.0).
O daemon AWDL continua GPL-3.0 (OWL) em processo separado, como no projeto original.

## Notas da fase 5

- O daemon de rádio é um laço único que alterna entre os anéis da placa e o pipe. Taxas fixas por enquanto: 6 Mb/s para gerência e multicast, 24 Mb/s para dados unicast em 5 GHz, 1 Mb/s em 2,4 GHz. Sem agregação, sem block ack e sem adaptação de taxa, que o rádio Realtek do winawdl tem. Isso limita a velocidade de transferência e é o próximo ponto de desempenho.
- `RADIO_LINK_SET_ADDRESS` reenvia `DEV_INFO_ACTIVE` com o endereço AWDL, para a placa reconhecer e confirmar quadros enviados a ele.
- Para rodar os dois daemons como LocalSystem pelo executor, `mt7902/detach` inicia um programa da pasta de build em segundo plano. `awdld.exe` e `wintun.dll` precisam ser copiados para `mt7902uild`.
- A latência do ping varia muito (6 a 456 ms), coerente com o rádio parado em um só canal enquanto o Mac alterna entre canais.
- Dependências novas na máquina: MinGW-w64 (WinLibs, via winget, escopo do usuário) e Wintun 0.14.1 em `deps\wintun`.

## Notas das fases 6 e 7

- Taxa de dados: a 24 Mb/s (`0x49`) tudo funciona; a 54 Mb/s (`0x4c`) o Mac acha o PC e responde a ping, mas os pedidos HTTPS dele ao receptor não completam. Sem agregação nem adaptação de taxa, o envio de 20 MB ficou em 1,1 MB/s.
- O AirDrop do Mac foi posto em "Todos" (`defaults write com.apple.sharingd DiscoverableMode Everyone`) para os testes.
- O rádio precisa do direito "Bloquear páginas na memória" (memória AWE para DMA). `scripts/grant-lock-pages.ps1` concede à conta; vale a partir do logon seguinte. Até lá os daemons sobem como LocalSystem pelo executor (`scripts/mt7902-awdl.ps1`) e o aplicativo usa o enlace já de pé.
- Convivência com o Wi-Fi: o desenho do winawdl é trocar o driver da placa ao ligar e devolver ao desligar (`app/src/Radio/WifiCard.cs`), o que só vale para a cópia instalada. Não é uso simultâneo.
- **Um fluxo espacial.** O `awdld` anuncia dois por padrão e a MT7902 informa um. Com dois, o Mac funcionava mas o iPhone não: de 15 pings voltava 1, e do iPhone só chegavam repetições. Com `-s 1`, 8 de 15 (o resto é o iPhone dormindo fora das janelas dele) e o AirDrop passou a funcionar.
- **Recepção em 80 MHz.** Em 5 GHz o rádio agora escuta o bloco de 80 MHz do canal (centro 155 para o 149) e diz isso no `HELLO`; a transmissão segue em 20 MHz. Sozinha, essa mudança não resolveu o iPhone, e fez subir muito a contagem de quadros com soma de verificação errada (centenas a cada 15 s), o que ainda não foi investigado.
- **Risco aberto, memória para DMA.** Uma vez o rádio não subiu com "not enough free memory below 4 GiB for the DMA slab"; na tentativa seguinte subiu. A busca por páginas físicas abaixo de 4 GiB pode falhar com a memória baixa ocupada.
- `radiod` aceita um intervalo de relatório: a cada N segundos imprime quem foi ouvido, com sinal, quadros de ação, dados para o PC e repetições. Foi o que mostrou o problema do iPhone.
- **Redes Wi-Fi salvas.** Trocar o driver dá ao adaptador uma identidade nova e as redes salvas ficam na pasta da antiga. O fluxo de desenvolvimento (scripts) não as trazia de volta; foram recuperadas com `netsh wlan add profile`. A cópia instalada já faz isso ao devolver a placa e agora também pede ao Windows para entrar na rede usada por último, porque sozinho ele não reconectou.
- **Rádio por tarefa do sistema.** `winawdl.exe --radio-wait` (conta do usuário) grava o pedido em `%ProgramData%\winawdl\radio.request`, dispara a tarefa e espera o estado final; `--radio-task` (LocalSystem) valida o pedido (um número e um nome de formato fixo) e roda o supervisor. O evento de parada fica no espaço `Global\`. Qualquer usuário com sessão interativa pode disparar a tarefa, que só executa os programas da pasta de instalação.

# Port do winawdl para a MediaTek MT7902

Status: 2026-11-16. Fases 0, 1 e 2 concluídas. O firmware roda na placa a partir de um programa comum.

## Registro de resultados

| Data | Fase | Resultado |
|---|---|---|
| 2026-11-16 | 1 | Aprovada. `regproxy` ligado à placa com Secure Boot ligado (certificado local). `probes/mt7902_chipid` leu BAR de 1 MiB em `0xf820300000`, mapeado direto, `LPCTL 0x0` (driver dono do chip) e `chip id 0x7902`, revisão `0x0000` |
| 2026-11-16 | 2 | Aprovada. `mt7902/fwboot` executou reset, anéis WFDMA por polling, patch (1 seção) e RAM (4 regiões, build 20251212032127). `MT_CONN_ON_MISC` passou de `0x0` para `0x3` (firmware pronto). DMA a partir de 512 páginas de 4 KiB abaixo de 4 GiB |

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

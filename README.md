<p align="center">
  <img src="docs/assets/windrop.png" width="128" alt="Logo do WinDrop">
</p>

<h1 align="center">WinDrop</h1>

<p align="center">
  AirDrop de verdade no Windows, pela placa Wi-Fi MediaTek MT7902.<br>
  Envia e recebe arquivos de iPhones e Macs falando AWDL, o protocolo sem fio do AirDrop.
</p>

<p align="center">
  <img src="docs/assets/demo-1-segurando.png" width="300" alt="WinDrop com um MacBook por perto e quatro arquivos prontos para enviar">
  <img src="docs/assets/ui-incoming-ask-dark.png" width="300" alt="Pedido de recebimento de um iPhone">
</p>

<p align="center"><sub>À esquerda, captura da janela real com um MacBook real. À direita, renderização do aplicativo com um aparelho de exemplo.</sub></p>

O WinDrop é um fork do [winawdl](https://github.com/raph559/winawdl), que faz AirDrop no Windows com uma placa Realtek RTL8852CE. Este fork troca o rádio: um driver em modo usuário para a **MediaTek MT7902**, escrito a partir do driver Linux mt76, que sobe o firmware da MediaTek, recebe e transmite quadros 802.11 crus e serve o mesmo protocolo de rádio que o resto do winawdl espera. Não usa servidor, nuvem nem aplicativo no iPhone: para os aparelhos Apple, o PC aparece no AirDrop como qualquer outro.

É código de pesquisa, feito e validado em uma máquina. Leia os [limites](#limites) antes de usar.

## O que funciona

Validado em 2026-11-16, em um desktop com a MT7902 (ASUS TUF GAMING A620M-PLUS WIFI), um MacBook Pro e um iPhone, todos no canal 149.

| Sentido | Resultado |
|---|---|
| PC para MacBook | 20 MB com SHA-256 idêntico nos dois lados |
| MacBook para PC | imagem arrastada no AirDrop do Mac, gravada em Downloads |
| iPhone para PC | foto enviada pela folha de compartilhamento |
| PC para iPhone | arquivo enviado e aceito no iPhone |

- **Botão direito no Explorer:** "Enviar via AirDrop" em qualquer arquivo.
- **Bandeja:** chave liga/desliga e opção de iniciar com o Windows já recebendo.
- **Sem pergunta do Windows ao ligar:** uma tarefa do sistema troca o driver da placa e sobe o rádio.
- **Wi-Fi de volta ao desligar:** em cerca de 10 segundos, com as redes salvas e reconectado.
- **Velocidade:** entre 3 e 5 MB/s do PC para o Mac a 1 metro (três envios de 20 MB).

## Telas

| Arquivos prontos para enviar | Pedido de recebimento | Arquivo recebido | Rádio desligado |
|---|---|---|---|
| ![Arquivos prontos para enviar](docs/assets/ui-holding-dark.png) | ![Pedido de recebimento](docs/assets/ui-incoming-ask-dark.png) | ![Arquivo recebido](docs/assets/ui-received-dark.png) | ![Rádio desligado](docs/assets/ui-radio-off-dark.png) |

Estas quatro são renderizações do próprio aplicativo com aparelhos de exemplo (`winawdl.exe --render`), feitas com a versão atual. O tema acompanha o Windows; em claro fica assim:

<p align="center">
  <img src="docs/assets/ui-holding-light.png" width="300" alt="Tema claro">
</p>

As capturas da janela real, com aparelhos reais, estão na demonstração abaixo.

## Demonstração

Um envio real, do PC para um MacBook, pelo WinDrop instalado. Os quatro arquivos estão em [docs/assets/samples](docs/assets/samples): três imagens geradas por IA para este fim e um arquivo de texto.

<p align="center">
  <img src="docs/assets/samples/praia.jpg" width="230" alt="Imagem de exemplo: praia">
  <img src="docs/assets/samples/cafe.jpg" width="230" alt="Imagem de exemplo: café e caderno">
  <img src="docs/assets/samples/cidade.jpg" width="230" alt="Imagem de exemplo: cidade ao entardecer">
</p>

| 1. Arquivos entregues ao WinDrop | 2. Pedido enviado, aguardando o aceite no Mac | Quando o aparelho não responde |
|---|---|---|
| ![Quatro arquivos prontos para enviar](docs/assets/demo-1-segurando.png) | ![Aguardando o aceite](docs/assets/demo-2-pedindo.png) | ![Sem resposta do aparelho](docs/assets/demo-sem-resposta.png) |

As três telas são capturas da janela real, com o MacBook real na lista. Depois do aceite, os quatro arquivos apareceram em Downloads do Mac com os mesmos tamanhos em bytes dos originais (323165, 435805, 386271 e 120), a 1,6 MB/s. A terceira tela é de uma tentativa anterior, com a tela do Mac apagada.

## Casos de uso

- **Do iPhone para o PC, sem cabo e sem nuvem.** Fotos e vídeos saem da folha de compartilhamento do iPhone direto para a pasta Downloads do Windows.
- **Do PC para o iPhone ou o Mac.** Botão direito em um arquivo no Explorer, "Enviar via AirDrop", um clique no aparelho.
- **Entre o Mac e o PC na mesma mesa.** O PC aparece na janela do AirDrop do Finder como mais um aparelho.
- **Sem internet e sem rede em comum.** O AirDrop é ponto a ponto: funciona sem roteador, sem conta e sem aplicativo instalado no lado Apple.
- **PC sempre visível.** Com "Start with Windows, radio on", o WinDrop sobe com o Windows, sem janela, já recebendo.

## Tecnologias avaliadas e escolhidas

| Decisão | Avaliado | Escolhido | Motivo |
|---|---|---|---|
| Como falar com aparelhos Apple | LocalSend e similares (exigem aplicativo no iPhone); AirDrop real | AirDrop real, por AWDL | Era o requisito: aparecer no AirDrop nativo do iPhone e do Mac, sem instalar nada neles |
| Placa Wi-Fi | Trocar por uma Realtek RTL8852CE, que o winawdl já suporta; portar para a MT7902 da placa-mãe | Portar para a MT7902 | Não trocar hardware |
| Referência para o chip | Engenharia reversa do driver Windows da MediaTek; driver Linux mt76 | mt76 (caminho `mt7921e`) | Código aberto, e a MT7902 entrou nele no Linux 7.1 |
| Onde roda o driver | Driver de kernel próprio; modo usuário sobre o `regproxy` do winawdl | Modo usuário | Um erro derruba um processo, não o Windows; o `regproxy` (UMDF) já existia e funciona com Secure Boot |
| Memória para DMA | Memória contígua de kernel; páginas físicas travadas por AWE | AWE, em páginas de 4 KiB abaixo de 4 GiB | A placa só aceita endereços de 32 bits, e AWE dispensa código de kernel |
| Interrupções | Interrupções da placa; consulta periódica dos anéis | Consulta (polling) | Não exige nada do driver de kernel |
| Pilha AWDL | Escrever do zero; OWL | OWL, como no winawdl | Implementação aberta e já validada com aparelhos Apple |
| Interface de rede no Windows | Driver NDIS próprio; Wintun | Wintun | Assinado, estável e usado pelo winawdl |
| Como o rádio ganha privilégio | Elevação (UAC) a cada vez; direito "Bloquear páginas na memória" na conta; tarefa agendada como LocalSystem | Tarefa agendada, restrita à conta que instalou | Sem pergunta do Windows ao ligar, e sem exigir novo logon |
| Wi-Fi e AirDrop | Uso simultâneo (exigiria um cliente Wi-Fi completo próprio); troca do driver ao ligar e desligar | Troca do driver | O uso simultâneo está fora do alcance do projeto hoje |
| Taxa de transmissão | OFDM 24 e 54 Mb/s; VHT em 20 e 80 MHz | VHT MCS 7 em 80 MHz, fixa | A mais rápida entre as que completaram os envios de teste |

## Testes

**Por fase**, cada uma com um critério medido na máquina:

| Fase | Critério | Resultado |
|---|---|---|
| 1. Driver toma a placa | ler o identificador do chip | `chip id 0x7902` |
| 2. DMA e firmware | firmware da MediaTek rodando, sem interrupções | pronto em até 4 tentativas, 10 de 10 inicializações |
| 3. Recepção | quadros nos canais 6, 44 e 149 | incluindo quadros AWDL de aparelhos Apple |
| 4. Transmissão | testemunha independente no ar | MacBook em modo monitor capturou 30 de 30 quadros OFDM no canal 149 |
| 5. AWDL | tráfego IPv6 com um aparelho Apple | `ping` ao Mac pela `awdl0`: 10 de 10 |
| 6. AirDrop | arquivos nos dois sentidos, Mac e iPhone | quatro sentidos validados, um deles com SHA-256 |
| 7. Integração | cópia instalada, ligar, enviar, desligar | ciclo completo, Wi-Fi de volta em cerca de 10 s |

**Automatizados:**

- `airdrop/tests`: 84 testes, todos passando (`python -m unittest discover -s tests`).
- `scripts\build-app.ps1 -Check`: 134 de 135. O que falha procura uma mensagem do `ping.exe` em inglês e este Windows está em português; falha igual sem as mudanças deste fork.

**Ferramentas de teste do fork:** `mt7902/fwboot`, `rxtest`, `txtest` e `radioclient` (rádio), `scripts/sniff-test.ps1` (Mac como testemunha), `scripts/rate-trial.ps1` (velocidade por taxa), `scripts/app-radio-switch.ps1` e `scripts/app-send.ps1` (o aplicativo, por automação de interface).

## Benchmarks

Envios reais de um arquivo de 20 MB do PC para um MacBook Pro a cerca de 1 metro, pelo WinDrop instalado, um envio por medição:

| Taxa de dados | Velocidade |
|---|---|
| OFDM 24 Mb/s | 1,5 MB/s |
| OFDM 54 Mb/s | 1,1 MB/s |
| VHT MCS 8, 20 MHz | 3,1 MB/s |
| VHT MCS 5, 80 MHz | 3,4 MB/s |
| **VHT MCS 7, 80 MHz (padrão)** | **5,2, 5,1 e 3,1 MB/s** |
| VHT MCS 9, 80 MHz | 2,3 MB/s |

Outras medidas: `ping` de 1400 bytes ao Mac pela `awdl0` com 0% de perda e mediana de 150 ms; sinal do Mac e do iPhone entre -52 e -60 dBm; Wi-Fi de volta cerca de 10 s depois de desligar.

São poucas amostras, com um aparelho e uma distância. O sentido Apple para PC e o iPhone na taxa padrão ainda não foram medidos.

## Como funciona

### As peças

```mermaid
flowchart TB
    subgraph usuario["Sessão do usuário"]
        explorer["Explorer<br>botão direito: Enviar via AirDrop"]
        app["WinDrop (WPF)<br>janela e bandeja"]
        engine["engine.py<br>AirDrop: mDNS, HTTPS, BLE"]
    end
    subgraph sistema["LocalSystem, por tarefa agendada"]
        supervisor["supervisor<br>troca o driver da placa"]
        awdld["awdld<br>AWDL (OWL)"]
        radiod["radiod<br>rádio da MT7902"]
    end
    subgraph kernel["Windows"]
        wintun["awdl0<br>interface Wintun"]
        regproxy["winawdl_regproxy<br>driver UMDF"]
    end
    placa[("MT7902<br>firmware MediaTek")]
    apple(["iPhone e Mac"])

    explorer -- "caminhos dos arquivos" --> app
    app -- "JSON por stdin e stdout" --> engine
    app -- "pedido de ligar e desligar" --> supervisor
    supervisor --> awdld
    supervisor --> radiod
    engine -- "IPv6 link-local" --> wintun
    wintun <--> awdld
    awdld -- "pipe nomeado, radio_link.h" --> radiod
    radiod -- "IOCTLs: registradores e DMA" --> regproxy
    regproxy <--> placa
    placa <-. "AWDL, canal 149" .-> apple
```

### Um envio, do clique à entrega

```mermaid
sequenceDiagram
    actor U as Você
    participant W as WinDrop
    participant R as Rádio (awdld e radiod)
    participant A as iPhone ou Mac

    U->>W: botão direito, Enviar via AirDrop
    W->>A: anúncio BLE, para acordar o AirDrop
    R-->>A: quadros AWDL de sincronismo
    W->>A: mDNS: quem recebe por perto?
    A-->>W: nome e endereço
    U->>W: clica no aparelho
    W->>A: HTTPS /Ask, com a lista de arquivos
    A-->>W: aceito
    W->>A: HTTPS /Upload, com os arquivos
    A-->>W: 200, recebido
    W-->>U: enviado
```

### A chave Radio

```mermaid
stateDiagram-v2
    [*] --> WiFi
    WiFi: Wi-Fi normal (driver da MediaTek)
    Trocando: tarefa do sistema troca o driver
    AirDrop: AirDrop no ar (rádio e AWDL)
    Devolvendo: devolve a placa e as redes salvas

    WiFi --> Trocando: chave ligada
    Trocando --> AirDrop: rádio de pé
    Trocando --> Devolvendo: rádio não subiu
    AirDrop --> Devolvendo: chave desligada, ou o WinDrop fechou
    Devolvendo --> WiFi: cerca de 10 s
```

### Pastas

- **`mt7902/`**: o que este fork acrescenta. Bring-up da placa, anéis de DMA por polling, carga do firmware, comandos MCU, modo monitor, transmissão com taxa fixa e o daemon `radiod`.
- **`awdl/`**: o daemon AWDL do winawdl sobre o [OWL](https://github.com/seemoo-lab/owl), sem alteração.
- **`airdrop/`**: receptor e remetente AirDrop em Python.
- **`app/`**: o aplicativo, com o botão direito, a chave na bandeja e o início com o Windows.
- **`driver/regproxy/`**: o driver UMDF que entrega a placa ao modo usuário.

O caminho completo, fase por fase, com o que deu errado e como foi resolvido, está em [docs/MT7902-PORT.md](docs/MT7902-PORT.md). O guia de uso está em [docs/WINDROP.md](docs/WINDROP.md).

## Limites

- **Wi-Fi e AirDrop não funcionam ao mesmo tempo no PC.** A placa é uma só: com o rádio ligado ela fala AWDL e o Wi-Fi do PC fica fora. Com cabo de rede o PC não perde a internet. O iPhone e o Mac não perdem nada.
- **Uma placa, uma máquina.** Só a MT7902 (`PCI\VEN_14C3&DEV_7902`), e só foi testado em um PC.
- **Só 5 GHz.** Em 2,4 GHz a placa só transmite a 1 Mb/s. O AirDrop usa o canal 149.
- **Velocidade modesta.** Taxa fixa, sem agregação nem adaptação: um aparelho longe demais para a taxa não recebe. O sentido Apple para PC não foi medido.
- **"Todos" no AirDrop.** O PC não é um contato da conta iCloud, então o iPhone precisa estar em "Todos por 10 minutos". Não há login com iCloud, "Somente contatos" nem Área de Transferência Universal: dependem de chaves que a Apple só emite para os aparelhos dela.
- **Fotos originais.** O receptor declara HEIF, HEVC e ProRes, como um Mac, para o iPhone não converter. Ainda falta confirmar com uma foto da câmera.
- **Arestas conhecidas.** O firmware às vezes pede parada do DMA ao subir (o rádio tenta de novo sozinho) e uma vez faltou memória física baixa para o DMA.

## Instalar

**Pelo instalador:** baixe o `WinDrop-0.5.0-setup.exe` em [Releases](https://github.com/matheusbgodoi/WinDrop/releases) e execute. Ele confere se o PC tem a placa MT7902 e para, sem mudar nada, se não tiver. O WinDrop avisa quando houver versão nova; a atualização é rodar o instalador novo por cima.

Não serve para qualquer Windows: só para PCs com a **MediaTek MT7902**. O instalador não é assinado por uma autoridade certificadora, então o SmartScreen pode avisar ao abrir. Foi testado neste PC atualizando uma cópia já instalada; a instalação do zero em outra máquina ainda não foi testada.

**Compilando do código:**

Requisitos: Windows 10 ou 11 de 64 bits, a placa MT7902, Python 3.10 ou mais novo e o .NET 10 Desktop Runtime. Para compilar: Visual Studio Build Tools com o WDK, o SDK do .NET 10 e MinGW-w64 (`winget install BrechtSanders.WinLibs.POSIX.UCRT --scope user`).

```powershell
git clone --recurse-submodules https://github.com/matheusbgodoi/WinDrop
cd WinDrop
git clone https://github.com/morrownr/mt76 ref\mt76     # firmware da MT7902
.\scripts\fetch-wintun.ps1
.\scripts\build-installer.ps1 -Build -AllowDirty
.\build\installer\winawdl-0.4.0\install.cmd             # pede administrador
```

A instalação assina o driver com um certificado criado no próprio PC, cria duas regras de firewall para endereços link-local, registra a tarefa que sobe o rádio e a que devolve o Wi-Fi na inicialização. `C:\Program Files\winawdl\uninstall.cmd` desfaz tudo.

## Usar

1. Abra o WinDrop pelo Menu Iniciar e ligue a chave **Radio**.
2. **Enviar:** botão direito em um arquivo, **Enviar via AirDrop**, e clique no aparelho. Ou arraste arquivos para a janela.
3. **Receber:** no iPhone ou no Mac, escolha o PC no AirDrop. O arquivo vai para Downloads.
4. Desligue a chave para o Wi-Fi do PC voltar.

## Créditos e licenças

- [winawdl](https://github.com/raph559/winawdl), de raph559: o projeto original, o aplicativo, o driver regproxy, o receptor AirDrop e o daemon AWDL para Windows. O README dele está em [docs/UPSTREAM-README.md](docs/UPSTREAM-README.md).
- [OWL](https://github.com/seemoo-lab/owl), do Secure Mobile Networking Lab: a implementação aberta do AWDL (GPL-3.0).
- [mt76](https://github.com/openwrt/mt76): o driver Linux que serviu de referência para a MT7902.
- [Wintun](https://www.wintun.net/), da WireGuard: a interface de rede virtual.

**Uso não comercial, com citação obrigatória da fonte.** O trabalho original deste fork (a pasta `mt7902/`, o nome, o logo, a documentação e as mudanças feitas no projeto original) está sob a [PolyForm Noncommercial 1.0.0](LICENSE-WINDROP.md): pode usar, estudar, modificar e redistribuir sem fins comerciais, mantendo o aviso de autoria; não pode vender nem embutir em produto pago.

O que vem de outros projetos continua sob a licença original (MIT para o winawdl, GPL-3.0 para o daemon AWDL), e este fork não pode restringi-las. [LICENSING.md](LICENSING.md) diz qual licença vale para cada parte. O firmware da MediaTek não faz parte deste repositório.

AirDrop, AWDL, iPhone, Mac e iCloud são marcas da Apple. Este projeto não é afiliado à Apple nem à MediaTek.

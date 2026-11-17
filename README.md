<p align="center">
  <img src="docs/assets/windrop.png" width="128" alt="Logo do WinDrop">
</p>

<h1 align="center">WinDrop</h1>

<p align="center">
  AirDrop de verdade no Windows, pela placa Wi-Fi MediaTek MT7902.<br>
  Envia e recebe arquivos de iPhones e Macs falando AWDL, o protocolo sem fio do AirDrop.
</p>

<p align="center">
  <img src="docs/assets/ui-incoming-ask-dark.png" width="300" alt="Pedido de recebimento de um iPhone">
  <img src="docs/assets/ui-holding-light.png" width="300" alt="Arquivos prontos para enviar, tema claro">
</p>

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

| Aparelhos por perto | Pedido de recebimento | Arquivo recebido |
|---|---|---|
| ![Aparelhos por perto](docs/assets/ui-holding-dark.png) | ![Pedido de recebimento](docs/assets/ui-incoming-ask-dark.png) | ![Arquivo recebido](docs/assets/ui-received-dark.png) |

As três imagens acima são renderizações do próprio aplicativo com aparelhos de exemplo (`winawdl.exe --render`). Esta é uma captura da cópia instalada, com um MacBook real por perto:

<p align="center">
  <img src="docs/assets/app-radio-on.png" width="300" alt="WinDrop instalado, com um MacBook real na lista">
</p>

## Como funciona

```
Explorer / bandeja
        |
   WinDrop (WPF)  ---- engine.py: AirDrop (mDNS, HTTPS, BLE)
        |                         |
        |                     awdl0 (Wintun)
        |                         |
   tarefa do sistema  ---->  awdld: AWDL (OWL)
                                  |  pipe nomeado, radio_link.h
                             radiod: rádio da MT7902
                                  |  IOCTLs
                         winawdl_regproxy (UMDF) -> registradores e DMA da placa
```

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

Requisitos: Windows 10 ou 11 de 64 bits, a placa MT7902, Python 3.10 ou mais novo e o .NET 10 Desktop Runtime. Para compilar: Visual Studio Build Tools com o WDK, o SDK do .NET 10 e MinGW-w64 (`winget install BrechtSanders.WinLibs.POSIX.UCRT --scope user`).

```powershell
git clone --recurse-submodules https://github.com/matheusbgodoi/winawdl-mt7902
cd winawdl-mt7902
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

O código em `mt7902/` é BSD-3-Clause-Clear. O restante segue as licenças de cada pasta (`LICENSE` na raiz e em `awdl/`, `airdrop/`, `radio/`). O firmware da MediaTek não faz parte deste repositório.

AirDrop, AWDL, iPhone, Mac e iCloud são marcas da Apple. Este projeto não é afiliado à Apple nem à MediaTek.

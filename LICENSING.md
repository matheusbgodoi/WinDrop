# Licenças do WinDrop

O WinDrop é um fork, então não tem uma licença só. Cada parte segue a licença de quem a escreveu, e este arquivo diz qual vale onde.

Isto é um resumo de boa-fé, não um parecer jurídico. Em caso de dúvida vale o texto de cada licença.

## O que é deste fork

Uso não comercial, com citação obrigatória da fonte: [PolyForm Noncommercial 1.0.0](LICENSE-WINDROP.md).

Vale para o trabalho original deste fork:

- a pasta `mt7902/` (o rádio da MediaTek MT7902);
- o nome WinDrop, o logo (`docs/assets/windrop.svg`) e o ícone;
- a documentação em `README.md` e `docs/` (menos `docs/UPSTREAM-README.md`);
- os scripts e ferramentas que este fork acrescentou em `scripts/` e `tools/`;
- as mudanças que este fork fez em arquivos do projeto original.

Na prática:

- **Pode:** usar, estudar, modificar e redistribuir para fins pessoais, de estudo, de pesquisa e sem fins lucrativos.
- **Não pode:** uso comercial. Isso inclui vender o programa, vender um fork dele ou embuti-lo em um produto ou serviço pago.
- **Tem que:** manter o aviso abaixo e o texto da licença em toda cópia ou obra derivada.

> Required Notice: Copyright (c) 2026 matheusbgodoi (https://github.com/matheusbgodoi/WinDrop)

Quem quiser uso comercial precisa de uma licença à parte, combinada com o autor.

## O que vem de outros projetos

Estas partes continuam sob a licença original. Este fork não pode mudá-las nem acrescentar restrições a elas.

| Parte | Origem | Licença |
|---|---|---|
| `app/`, `driver/`, `installer/`, `scripts/` (o que já existia) | [winawdl](https://github.com/raph559/winawdl), de Raphaël Richaud | MIT, em [LICENSE](LICENSE) |
| `airdrop/` | winawdl, adaptado do omdrop | MIT, em [airdrop/LICENSE](airdrop/LICENSE) |
| `awdl/` e `upstream/owl` | winawdl e [OWL](https://github.com/seemoo-lab/owl) | GPL-3.0 ou posterior, em [awdl/LICENSE](awdl/LICENSE) |
| `radio/` e `upstream/devourer` | winawdl e devourer | GPL-2.0, em [radio/LICENSE](radio/LICENSE). Não entram no instalador do WinDrop |
| `wintun.dll` | [Wintun](https://www.wintun.net/), da WireGuard | licença própria dos binários, incluída no instalador |
| Firmware da MT7902 | MediaTek, pelo linux-firmware | licença da MediaTek, incluída no instalador. Não está neste repositório |

Consequências que convém saber:

- O `awdld.exe` é GPL-3.0. Quem recebe o instalador pode usar, modificar e redistribuir esse programa nos termos da GPL, inclusive comercialmente. A restrição de uso não comercial não alcança essa parte.
- O mesmo vale para o que é MIT: a restrição cobre só o que este fork escreveu.
- O código em `mt7902/` foi escrito a partir do estudo do driver Linux [mt76](https://github.com/openwrt/mt76), que é BSD-3-Clause-Clear e ISC. As sequências de inicialização e os formatos de comando vêm dele.

## Marcas

AirDrop, AWDL, iPhone, Mac e iCloud são marcas da Apple. MediaTek é marca da MediaTek. Este projeto não é afiliado a nenhuma das duas.

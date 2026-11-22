<h1 align="center">🖥️ Desk Buddy — Embrapa I/O</h1>

<p align="center">
  <b>Um painel de mesa que mostra, em tempo real, a saúde da plataforma <a href="https://status.embrapa.io">Embrapa I/O</a></b><br>
  clusters, hosts, serviços, painéis e servidores de GPU — num pequeno display colorido com toque.
</p>

<p align="center">
  <img src="docs/images/using.gif" width="520" alt="Desk Buddy em uso">
</p>

<p align="center">
  <img alt="Licença MIT" src="https://img.shields.io/badge/licen%C3%A7a-MIT-green">
  &nbsp;<img alt="Plataforma" src="https://img.shields.io/badge/hardware-ESP32%20CYD-blue">
</p>

---

## O que é

O **Desk Buddy** fica na sua mesa e, sem que você precise abrir nada, mostra se a **plataforma
Embrapa I/O está no ar**. Ele lê o monitoramento oficial (**Gatus**, em
[status.embrapa.io](https://status.embrapa.io)) e exibe o estado dos cinco grupos de lá —
**clusters**, **hosts**, **serviços**, **painéis** e **GPU servers** — com a mesma linguagem do
painel (`Healthy` / `N off`). Tem **navegação por toque**, **relógio**, e é **configurado pela
própria tela** (Wi-Fi, fuso, brilho) — sem cabo nem computador.

É feito com um **Cheap Yellow Display (CYD / ESP32)** dentro de um case impresso em 3D. Sempre
ligado, alimentado por USB.

## As telas

| | |
|---|---|
| <img src="docs/images/screen-home.png" width="330"> | **Início** — seis cards: um por grupo (quantos estão no ar e o status, `Healthy` / `N off`) e **Configurar**. O veredito geral fica no alto, ao lado do relógio. Toque num card para abrir a listagem dele. |
| <img src="docs/images/screen-list.png" width="330"> | **Listagem** — os itens do grupo, com status e latência; o que estiver fora do ar aparece em destaque no topo. A lista rola, e o botão **Início** volta para os cards. |
| <img src="docs/images/screen-panels.png" width="330"> | **Painéis** e **GPU Servers** — os grupos novos do Gatus têm card e listagem próprios. |
| <img src="docs/images/screen-config.png" width="330"> | **Configurar** — Wi-Fi, fuso horário, intervalo de atualização e brilho. Rolando, aparecem o sinal do Wi-Fi, o tempo ligado, a última atualização e a versão. |
| <img src="docs/images/screen-offline.png" width="330"> | **Sem dados** — se a leitura falha por mais de 3 minutos, a tela deixa de dizer `Healthy`: mostra `Offline` e há quanto tempo os números estão parados. |

> As imagens acima saem do simulador (`tools/sim/run.sh`), que roda o próprio firmware no computador.

**O LED** acompanha o veredito: **verde** = tudo no ar, **vermelho** = algo fora do ar, **azul** = sem
dados (sem Wi-Fi ou sem resposta do monitoramento).

**Se o Wi-Fi cair**, o Desk Buddy reconecta sozinho: tenta de novo em 15 s, 30 s e depois a cada
minuto; se a rede diz estar conectada mas nada responde, refaz a conexão; e, depois de 15 minutos sem
rede, reinicia. Não é preciso abrir a Configuração.

## 🛒 O que você vai precisar

| Item | Onde comprar | Preço aprox. |
|---|---|---|
| **Placa CYD** (ESP32-2432S028R, 2.8" touch) | [AliExpress](https://pt.aliexpress.com/item/1005011857768315.html) | ~US$ 10 + impostos |
| **Conector USB-C fêmea com DuPont** (alimentação) | [AliExpress](https://pt.aliexpress.com/item/1005005823388148.html) | ~US$ 0,50/un (kit de 10) + impostos |
| Carregador **USB 5 V** + cabo USB-C | (provavelmente você já tem) | — |

> O conector USB-C já vem com plugue **DuPont**, que encaixa nos jumpers que acompanham a placa —
> liga em **VIN (+5 V)** e **GND**. **Confira a polaridade** antes de energizar. Detalhes em
> [docs/tech-spec.md](docs/tech-spec.md).

## 🖨️ Imprimindo o case

Usamos o modelo **CYD Desk Buddy** da comunidade (MakerWorld):
**https://makerworld.com/pt/models/2787810-cyd-desk-buddy-for-bambu-lab-home-assistant**

<p align="center">
  <img src="docs/images/case-3d-print.gif" width="420" alt="Impressão 3D do case">
</p>

## 🔧 Instalando o firmware

> **Gravador web — 1 clique, sem instalar nada:** abra **[buddy.embrapa.io](https://buddy.embrapa.io)**
> no **Google Chrome** ou **Microsoft Edge** (no computador), conecte o CYD por USB e clique em **Instalar**.

**Já tem um Desk Buddy e quer atualizar?** O mesmo gravador serve. Ele regrava tudo, inclusive a
configuração: depois da atualização, o aparelho pede o Wi-Fi de novo. Para atualizar mantendo a
configuração, grave pelo PlatformIO (`pio run -t upload`).

**A porta serial não aparece?** O computador não está vendo a placa. Ligue direto numa porta USB (sem
hub nem dock) e troque o cabo: muitos só carregam. Mais dicas em
[docs/tech-spec.md](docs/tech-spec.md#gravação--pontos-de-atenção-cydch340).

Quer compilar do código ou customizar? Veja **[docs/tech-spec.md](docs/tech-spec.md)**.

**Na primeira vez que ligar**, o Desk Buddy abre sozinho a tela de **Configuração**: digite o nome e
a senha do seu **Wi-Fi (2,4 GHz)** pela tela de toque, salve, e pronto — ele se conecta e começa a
mostrar o status. Depois, dá para trocar Wi-Fi, fuso horário, intervalo de atualização e brilho a
qualquer momento no card **Configurar**.

## 🛠️ Quer customizar?

O firmware é aberto e dá para adaptar (outra fonte de dados, cores, telas, fusos). Veja a
**[documentação técnica](docs/tech-spec.md)**.

## Licença e créditos

[MIT](LICENSE) · Desenvolvido pela **Embrapa Gado de Corte**.
Usa [LVGL](https://lvgl.io), [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) e a API do
[Gatus](https://github.com/TwiN/gatus).

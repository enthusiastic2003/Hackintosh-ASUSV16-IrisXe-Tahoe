# Aceleração da Iris Xe no macOS Tahoe — ASUS Vivobook V16 (V3607VH)

Aceleração gráfica de verdade (**Metal, Core Animation, Liquid Glass**) para a **Intel Graphics do Core 7 240H (Raptor Lake-H, 8086:A7AB, 64 EU)** no **macOS Tahoe 26.7.1**, num hackintosh com OpenCore.

A Apple nunca fez driver para o Raptor Lake. Este projeto roda **o driver que a própria Apple fez para a Iris Xe do Tiger Lake** (GPUDriversIntel 16.0.32, de Macs que nunca foram lançados) na GPU mais nova, porque as duas têm a mesma arquitetura (Xe-LP, Gen12). Um **NootedGreen modificado** (plugin do Lilu) corrige as diferenças no kernel, e um conjunto de patches de user space adapta esse driver de 2020 ao Metal do Tahoe.

> 🚧 **Projeto em andamento: este é um port e ainda não foi inicializado.** A pilha de GPU foi desenvolvida e
> verificada em outro notebook (o projeto upstream); o que está marcado como funcionando foi confirmado lá, não no
> Vivobook V16. Veja o [CHANGELOG.md](CHANGELOG.md) e o [docs/ASUS-V16.md](docs/ASUS-V16.md).

> ⚠️ **Experimental.** Ainda acontecem travamentos da GPU e kernel panics de vez em quando. Tenha sempre um segundo disco de boot (ou o instalador no USB). Leia [docs/STATUS.md](docs/STATUS.md) antes de começar.

## O que funciona

| Área | Status |
|---|---|
| Mesa, Dock, janelas, desfoque "Liquid Glass" | ✅ acelerado pela GPU (Metal) |
| Resolução nativa (3840×2400) e escala HiDPI | ✅ (troque com `tools/xe-res`, não pelos Ajustes) |
| Frequência da GPU | ✅ 1100 MHz sob carga, repouso em RC6 |
| Imagens HEIC/YUV (papéis de parede, fotos) | ✅ |
| Ícones de apps, Finder, miniaturas | ✅ |
| YouTube | ✅ no Chrome (com GPU, até 4K) · ✅ no Safari com uma saída de áudio (Bluetooth ou o BlackHole virtual, `tools/build-blackhole.sh`) |
| Gravação de tela | ✅ |
| Decodificação de vídeo por hardware | 🚧 em andamento (desligada por padrão; o vídeo é decodificado na CPU) |
| Teclas de brilho | ❌ use `tools/xe-brilho` |
| Ajustes › Telas | ⚠️ pode dar pânico |
| Travamento do WindowServer | ⚠️ às vezes antes do login e em algumas trocas de escala da tela (reiniciar resolve) |

Som interno (SoundWire) e câmera **não funcionam** no macOS neste notebook.

## Documentação

- [Guia de instalação](docs/INSTALL.md)
- [Patches do kernel](docs/KERNEL-PATCHES.md): cada correção, o problema que resolve e o boot-arg que a desliga
- [Metal, libxedrv e compilador de shaders](docs/METAL.md), incluindo a correção dos ícones sumidos
- [Status e problemas conhecidos](docs/STATUS.md)
- [Como depuramos](docs/DEBUGGING.md): as ferramentas de teste que acharam cada bug

A documentação técnica está em inglês. Os comentários do código estão, em boa parte, em português.

## Créditos e licença

Base: **NootedGreen** de StezzaPilot (licença *Thou Shalt Not Profit 1.0*, que vale também para este fork). Referências: WhateverXe e o fork do OCLP de b00t0x, IntelIGPU-Tahoe, acidanthera, OpenIntelWireless, VoodooI2C. Fork de [JulioNectar/Hackintosh-XPS13Plus-IrisXe-Tahoe](https://github.com/JulioNectar/Hackintosh-XPS13Plus-IrisXe-Tahoe), onde essa pilha de GPU foi desenvolvida e verificada, em parceria com o Claude (Anthropic).

**Nenhum binário da Apple está incluído.** Você precisa da sua própria cópia dos bundles 16.0.32.

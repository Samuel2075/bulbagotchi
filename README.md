# Bulbagotchi (Kindle C + GTK+-2.0)

Estrutura base de um Tamagotchi do Bulbasaur para Kindle desbloqueado (com KUAL).

## Estrutura de pastas
- `src/main.c`: codigo-fonte em C com GTK+-2.0 e persistencia em `save.dat`.
- `Makefile`: automatiza a compilacao cruzada para ARM e copia o binario para `dist/bulbagotchi/bin/bulbagotchi`.
- `dist/bulbagotchi/`: pasta pronta para ser copiada para `/mnt/us/extensions/bulbagotchi/` no Kindle.
  - `config.xml` e `menu.json`: integracao com o KUAL.
  - `assets/*.png`: sprites iniciais de placeholder (224x224, alto contraste). Substitua pelos seus sprites pixel art favoritos.

## Como compilar e instalar
1. Compile usando o toolchain/SDK ARM do seu Kindle (ajuste `CC` se usar outro prefixo, como `aarch64-linux-gnu-gcc`):
   ```bash
   make
   ```
2. Copie o conteudo de `dist/bulbagotchi` para `/mnt/us/extensions/bulbagotchi` no Kindle e garanta permissao de execucao (`chmod +x /mnt/us/extensions/bulbagotchi/bin/bulbagotchi`).
3. Abra o **KUAL** no Kindle e selecione **Bulbagotchi -> Abrir Bulbagotchi**.

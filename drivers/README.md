# drivers

Исходники драйверов Haiku, которые правятся в рамках проекта.

- `usb_audio/` - драйвер USB Audio Class (src/add-ons/kernel/drivers/audio/usb).
  Первый коммит - исходники без изменений, дальше правки отдельными коммитами. Описание изменений: `usb_audio/README.md`.
- Патчи для драйвера hda и add-on multi_audio лежат в `docs/hda-two-inputs/`.

Не собирается вместе с BeAE: файлы кладутся в дерево Haiku
(`src/add-ons/kernel/drivers/audio/usb`), сборка `jam -q usb_audio`.

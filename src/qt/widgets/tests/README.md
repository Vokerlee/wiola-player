# Widget contracts and journeys

These suites exercise the Qt Widgets binding layer, including models/selection, commands, controls, accessibility, painting, layouts, themes and end-to-end process lifetime. Widget-only fixtures live in [support](support); shared Qt storage fixtures live with the Qt test owner. Portable sorting policy and QtCore projection lifecycle have separate targets in their actual owners.

The projection benchmark measures the real table binding and remains an explicit artifact-producing target, not a wall-clock CI assertion. Native Windows journeys consume the same CMake install component as before; encoded media is provided by the codec owner's scoped fixtures.

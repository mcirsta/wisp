# StartupCafe.ro Analysis

**URL**: https://startupcafe.ro/  
**Comparison**: vs ctvnews.ca (Rendering Issues)

## Findings

1. **Layout Structure**
   - Uses **Tailwind CSS** utility classes (`flex`, `flex-col`, `w-full`, `mt-2`)
   - Layout properties (`display`, `width`, `height`, `position`) are **hardcoded** in classes
   - **Result**: Layout is robust. Even without CSS variable support, the grid and flexbox structure remains intact.

2. **CSS Variable Usage**
   - Extensive use of variables for **colors and typography**
   - Over 300 root variables (e.g., `--wp--preset--color--`, `--wp--preset--font-size--`)
   - **NOT** used for core layout logic like `display: var(...)` or `position: var(...)`

3. **Resilience**
   - If CSS variables fail to resolve:
     - The site **will render correctly** (structure-wise)
     - Colors and fonts might fallback or be missing (aesthetic issue only)
   - This is unlike `ctvnews.ca`, where `display: var(...)` causes the layout to collapse vertically.

## Conclusion
`startupcafe.ro` is a good example of a "safe" modern site for wisp-qt. It uses modern CSS (flex/grid) but in a static way that wisp's engine can handle (mostly). The ctvnews.ca issue is specific to "dynamic layout variables," which is a more aggressive/recent pattern.

---
trigger: always_on
---

# Engineering Standards & Consultation
AI agents must prioritize high-quality, root-cause fixes and maintain active communication with the user.

## Rules:
1. **User Consultation**: Once you have reached a conclusion or identified potential fixes, you **must** consult with the USER regarding the available options and trade-offs.
2. **Prioritize Quality Over Hacks**: Avoid proposing or implementing "hacks" that merely hide the problem or address symptoms rather than the root cause.
   - Example: Do not implement a null check for an object that, by design, should never be null. Instead, investigate why it is null.
3. **Transparent Decision Making**: Clearly explain the advantages and disadvantages of each proposed option to allow the USER to make an informed decision.
4. **Avoid Regressions**: Be extremely careful about regressions. Propose and implement unit tests that target real code paths.
5. **Testing Philosophy**: Prioritize testing actual logic. Use mocks only for support where they are strictly necessary to isolate the system under test.
6. **Code Restructuring**: Do not hesitate to propose significant restructures or rewrites if they yield clear benefits in quality, simplicity, or performance. You **must** consult with the USER and get approval before proceeding with any major code restructure or rewrite.

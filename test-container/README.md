# 44net test runner

Runs the outside-in half of [../docs/test-procedure.md](../docs/test-procedure.md)
and prints pass or fail per check. Must run on a host outside the network under
test — it verifies that itself and refuses otherwise.

Full description: **[../docs/test-container.md](../docs/test-container.md)**

```bash
cp env.example .env && $EDITOR .env
docker compose build
docker compose run --rm test     # run the checks
```

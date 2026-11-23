# Review contract tests

Fixtures check complete contracts, explicitly named retirements, missing/comment-only/duplicate fields and invalid published subjects. The expected naming convention is independent of the current Git history.

```mermaid
flowchart LR
  Fixtures[Independent review and subject examples] --> Contract[Review validation]
  Contract --> Accepted[Concrete review fields]
  Contract --> Rejected[Missing contract or invalid name]
```

Run `python3 -m unittest discover -s tools -t . -p 'test_*.py'`.

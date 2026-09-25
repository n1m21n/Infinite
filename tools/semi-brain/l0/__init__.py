"""L0 of the semi-brain: Claude's reasoning source. Claims, decisions with their rationale,
rejected alternatives and open questions that a session wants the next one to know, each with
evidence, a confidence and a trust tier. Weighted so it can never outrank verified code, and
never confirmed by being retrieved (see store.py)."""

"""Tests for adapter framework: cache, base adapter, scheduler."""

import asyncio
import time

import pytest

from adapters import (
    AdapterConfig,
    AdapterScheduler,
    AdapterStatus,
    BaseAdapter,
    TTLCache,
)


# --- TTL Cache ---------------------------------------------------------------

class TestTTLCache:
    def test_set_and_get(self):
        c = TTLCache()
        c.set("k", {"v": 1}, ttl=60)
        assert c.get("k") == {"v": 1}

    def test_get_missing_returns_none(self):
        c = TTLCache()
        assert c.get("nope") is None

    def test_expired_returns_none(self):
        c = TTLCache()
        c.set("k", {"v": 1}, ttl=0)  # expires immediately
        time.sleep(0.01)
        assert c.get("k") is None

    def test_keys(self):
        c = TTLCache()
        c.set("a", {}, ttl=60)
        c.set("b", {}, ttl=60)
        assert sorted(c.keys()) == ["a", "b"]

    def test_delete(self):
        c = TTLCache()
        c.set("k", {"v": 1}, ttl=60)
        c.delete("k")
        assert c.get("k") is None

    def test_get_age(self):
        c = TTLCache()
        c.set("k", {}, ttl=60)
        age = c.get_age("k")
        assert age is not None
        assert age < 1.0

    def test_get_age_missing(self):
        c = TTLCache()
        assert c.get_age("nope") is None

    def test_clear(self):
        c = TTLCache()
        c.set("a", {}, ttl=60)
        c.set("b", {}, ttl=60)
        c.clear()
        assert c.keys() == []


# --- Base Adapter (via concrete subclass) ------------------------------------

class FakeAdapter(BaseAdapter):
    """Test adapter that returns canned data or raises on demand."""

    def __init__(self, name="fake", fail=False, data=None, config=None):
        super().__init__(name, config)
        self.fail = fail
        self.data = data or {"raw": True}
        self.poll_count = 0

    async def poll(self):
        self.poll_count += 1
        if self.fail:
            raise RuntimeError("poll failed")
        return self.data

    def parse(self, raw):
        return {"parsed": True, **raw}


class TestBaseAdapter:
    def test_backoff_delay(self):
        a = FakeAdapter(config=AdapterConfig(backoff_base=2.0, backoff_max=30.0))
        assert a.backoff_delay(0) == 1.0   # 2^0
        assert a.backoff_delay(1) == 2.0   # 2^1
        assert a.backoff_delay(2) == 4.0   # 2^2
        assert a.backoff_delay(10) == 30.0  # capped

    def test_repr(self):
        a = FakeAdapter(name="test")
        assert "test" in repr(a)
        assert "idle" in repr(a)


# --- Scheduler ---------------------------------------------------------------

class TestScheduler:
    @pytest.mark.asyncio
    async def test_poll_once_success(self):
        cache = TTLCache()
        sched = AdapterScheduler(cache=cache)
        adapter = FakeAdapter(data={"temp": 72})
        sched.register(adapter)

        result = await sched.poll_once(adapter)
        assert result is not None
        assert result["parsed"] is True
        assert result["temp"] == 72
        assert adapter.state.status == AdapterStatus.OK
        assert cache.get("fake") is not None

    @pytest.mark.asyncio
    async def test_poll_once_failure(self):
        cache = TTLCache()
        sched = AdapterScheduler(cache=cache)
        adapter = FakeAdapter(
            fail=True,
            config=AdapterConfig(max_retries=1, backoff_base=0.01),
        )
        sched.register(adapter)

        result = await sched.poll_once(adapter)
        assert result is None
        assert adapter.state.status == AdapterStatus.ERROR
        assert adapter.state.consecutive_failures == 1
        assert adapter.poll_count == 2  # initial + 1 retry

    @pytest.mark.asyncio
    async def test_poll_once_retries_then_succeeds(self):
        cache = TTLCache()
        sched = AdapterScheduler(cache=cache)
        adapter = FakeAdapter(
            fail=True,
            config=AdapterConfig(max_retries=2, backoff_base=0.01),
        )
        sched.register(adapter)

        # Fail first attempt, then succeed on retry
        original_poll = adapter.poll

        async def flaky_poll():
            if adapter.poll_count < 2:  # fail first call
                adapter.poll_count += 1
                raise RuntimeError("transient")
            adapter.poll_count += 1
            return {"recovered": True}

        adapter.poll = flaky_poll
        result = await sched.poll_once(adapter)
        assert result is not None
        assert result["recovered"] is True
        assert adapter.state.status == AdapterStatus.OK

    def test_register_duplicate_raises(self):
        sched = AdapterScheduler()
        sched.register(FakeAdapter(name="a"))
        with pytest.raises(ValueError, match="already registered"):
            sched.register(FakeAdapter(name="a"))

    def test_list_adapters(self):
        sched = AdapterScheduler()
        sched.register(FakeAdapter(name="weather"))
        adapters = sched.list_adapters()
        assert len(adapters) == 1
        assert adapters[0]["name"] == "weather"
        assert adapters[0]["status"] == "idle"

    @pytest.mark.asyncio
    async def test_start_and_stop(self):
        sched = AdapterScheduler()
        adapter = FakeAdapter(
            config=AdapterConfig(poll_interval=0.05),
        )
        sched.register(adapter)

        await sched.start()
        await asyncio.sleep(0.15)
        await sched.stop()

        assert adapter.state.total_polls >= 1
        assert adapter.state.status == AdapterStatus.OK

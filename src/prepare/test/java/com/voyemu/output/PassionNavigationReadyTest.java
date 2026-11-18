package com.voyemu.output;

public final class PassionNavigationReadyTest {
    static class Fake implements PassionNavigationReady.Environment {
        boolean primary = true, secondary, addDuringListen, fail, failListen, failDeadline;
        int shows, warnings, listened, removed, cancelled, finished;
        Runnable change, timeout;

        public boolean ready() {
            return primary && secondary;
        }

        public void listen(Runnable r) {
            change = r;
            listened++;

            if (failListen) {
                throw new IllegalArgumentException("listen");
            }

            if (addDuringListen) {
                secondary = true;
            }
        }

        public void unlisten() {
            if (change != null) {
                removed++;
                change = null;
            }
        }

        public void deadline(Runnable r) {
            timeout = r;

            if (failDeadline) {
                throw new IllegalArgumentException("deadline");
            }
        }

        public void cancelDeadline() {
            if (timeout != null) {
                cancelled++;
                timeout = null;
            }
        }

        public void showOriginal() {
            shows++;

            if (fail) {
                throw new IllegalArgumentException("owner failure");
            }
        }

        public void unavailable() {
            warnings++;
        }

        PassionNavigationReady.Job job() {
            return new PassionNavigationReady.Job(this, () -> finished++);
        }
    }

    static void check(boolean value) {
        if (!value) {
            throw new AssertionError();
        }
    }

    public static void main(String[] args) {
        checkReadiness();
        checkFailures();
        checkRequests();
        System.out.println(
                "PASS 12 readiness/lifecycle cases; real Android/OEM invocation not executed");
    }

    private static void checkReadiness() {
        Fake ready = new Fake();

        ready.secondary = true;
        ready.job().start();
        check(ready.shows == 1 && ready.listened == 0 && ready.finished == 1);
        Fake delayed = new Fake();
        PassionNavigationReady.Job job = delayed.job();
        job.start();
        check(delayed.shows == 0 && delayed.listened == 1);
        delayed.secondary = true;
        Runnable old = delayed.change;
        old.run();
        old.run();
        check(
                delayed.shows == 1
                        && delayed.removed == 1
                        && delayed.cancelled == 1
                        && delayed.finished == 1);
        Fake race = new Fake();
        race.addDuringListen = true;
        race.job().start();
        check(race.shows == 1 && race.removed == 1);
        Fake missingPrimary = new Fake();
        missingPrimary.primary = false;
        missingPrimary.secondary = true;
        missingPrimary.job().start();
        check(missingPrimary.shows == 0);
        missingPrimary.primary = true;
        missingPrimary.change.run();
        check(missingPrimary.shows == 1);
        Fake timeout = new Fake();
        PassionNavigationReady.Job timed = timeout.job();
        timed.start();
        Runnable stale = timeout.change;
        timeout.timeout.run();
        timeout.secondary = true;
        stale.run();
        timed.expire();
        check(
                timeout.shows == 0
                        && timeout.warnings == 1
                        && timeout.finished == 1
                        && timeout.removed == 1
                        && timeout.cancelled == 1);
    }

    private static void checkFailures() {
        Fake ownerError = new Fake();

        ownerError.secondary = true;
        ownerError.fail = true;
        boolean propagated = false;

        try {
            ownerError.job().start();
        } catch (IllegalArgumentException expected) {
            propagated = expected.getMessage().equals("owner failure");
        }

        check(propagated && ownerError.shows == 1 && ownerError.finished == 1);
        Fake irrelevant = new Fake();
        irrelevant.job().start();
        irrelevant.change.run();
        check(irrelevant.shows == 0 && irrelevant.warnings == 0);

        for (boolean listenFailure : new boolean[] {true, false}) {
            Fake setup = new Fake();
            setup.failListen = listenFailure;
            setup.failDeadline = !listenFailure;
            PassionNavigationReady.Job failed = setup.job();
            boolean caught = false;

            try {
                failed.start();
            } catch (IllegalArgumentException expected) {
                caught = true;
            }

            check(caught && failed.stopped && setup.removed == 1 && setup.finished == 1);
        }
    }

    private static void checkRequests() {
        PassionNavigationReady.Requests requests = new PassionNavigationReady.Requests();
        Object service = new Object();

        requests.add(service);
        requests.add(service);
        check(requests.services.size() == 1);
        requests.remove(service);
        check(requests.empty());
        requests.add(null);
        requests.add(service);
        requests.remove(service);
        check(!requests.empty());
        requests.clear();
        check(requests.empty());
        Fake destroyed = new Fake();
        PassionNavigationReady.Job pending = destroyed.job();
        requests.add(service);
        pending.start();
        Runnable staleService = destroyed.change;
        requests.cancel(service, pending);
        destroyed.secondary = true;
        staleService.run();
        check(destroyed.shows == 0 && destroyed.removed == 1 && destroyed.finished == 1);
        Fake shared = new Fake();
        PassionNavigationReady.Job sharedJob = shared.job();
        requests.add(service);
        requests.add(null);
        sharedJob.start();
        requests.cancel(service, sharedJob);
        check(!sharedJob.stopped);
        shared.secondary = true;
        shared.change.run();
        check(shared.shows == 1 && shared.finished == 1);
        requests.clear();
    }
}

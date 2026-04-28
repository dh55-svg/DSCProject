#pragma once
#include <QHash>
#include <QDateTime>

class ChatteringGuard {
public:
    // Check if this tag alarm is chattering. Returns true if it should be suppressed.
    bool check(quint32 tagId, int maxRepeatsPerMin = 3) {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        auto& st = m_state[tagId];

        if (st.windowStart == 0 || (now - st.windowStart) > 60000) {
            st.count = 1;
            st.windowStart = now;
            return false;
        }

        st.count++;
        if (st.count >= maxRepeatsPerMin) {
            st.autoShelved = true;
            return true;
        }
        return false;
    }

    void reset(quint32 tagId) { m_state.remove(tagId); }
    bool isAutoShelved(quint32 tagId) const { return m_state.value(tagId).autoShelved; }

private:
    struct State {
        int count = 0;
        qint64 windowStart = 0;
        bool autoShelved = false;
    };
    QHash<quint32, State> m_state;
};

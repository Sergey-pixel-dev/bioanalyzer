#include "ai/AiPageBase.h"

#include "navigation/AppContext.h"
#include "ui_aipages.h"

#include <QLabel>
#include <QVBoxLayout>

AiPageBase::AiPageBase(AppContext *c, const QString &t, QWidget *p) : QWidget(p), m_context(c)
{
    Ui::AiPagesForm form;
    form.setupUi(this);
    auto *l = qobject_cast<QVBoxLayout *>(layout());
    if (!l)
        l = new QVBoxLayout(this);
    l->insertWidget(0, new QLabel(tr("AI / %1").arg(t), this));
    m_status = new QLabel(tr("Ready"), this);
    l->insertWidget(1, m_status);
}

void AiPageBase::setStatus(const QString &text, Notifications::Level level)
{
    if (m_context && m_context->notifications()) {
        m_context->notifications()->status(objectName(), text, level);
    } else if (m_status) {
        m_status->setText(text);
    }
}

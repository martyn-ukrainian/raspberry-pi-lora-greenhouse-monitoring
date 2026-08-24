"""air_temperature and air_humidity become nullable

Причина не в схемі, а в тому, що вона робила з даними. Вузол, у якого мовчить
SHT31, шле пакет БЕЗ полів повітря — навмисно, бо нуль сервер записав би як
справжні 0 °C. Але колонки були NOT NULL, тож adapter відкидав такий пакет
цілком, разом із цілком справним виміром ґрунту.

На вузлі stage3-lowpower це означало нуль рядків у базі при повністю робочому
радіо: у логу лише `Dropping measurement ... air sensor gave no data`. Тобто
поломка одного датчика знищувала дані іншого.

Revision ID: c4f1a8e37b02
Revises: afea071620cb
Create Date: 2026-08-24 16:05:00.000000

"""
from typing import Sequence, Union

from alembic import op
import sqlalchemy as sa


revision: str = 'c4f1a8e37b02'
down_revision: Union[str, Sequence[str], None] = 'afea071620cb'
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    # SQLite не вміє ALTER COLUMN — batch_alter_table перестворює таблицю з
    # перенесенням даних. На ~70 тис. рядків це секунди.
    with op.batch_alter_table('measurement') as batch:
        batch.alter_column('air_temperature', existing_type=sa.Float(), nullable=True)
        batch.alter_column('air_humidity', existing_type=sa.Float(), nullable=True)


def downgrade() -> None:
    # Назад — тільки якщо NULL-ів ще нема: інакше рядки без повітря не
    # вміщаються в NOT NULL, і відкат має впасти голосно, а не мовчки їх з'їсти.
    with op.batch_alter_table('measurement') as batch:
        batch.alter_column('air_temperature', existing_type=sa.Float(), nullable=False)
        batch.alter_column('air_humidity', existing_type=sa.Float(), nullable=False)

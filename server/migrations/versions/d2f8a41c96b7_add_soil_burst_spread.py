"""add soil_min, soil_max, soil_n to measurement

Розкид усередині бурста. Прошивка greenhouse-node-v2 везе ці поля з першого
дня, але доти вони гинули в трьох місцях підряд — модель адаптера, тіло
запиту, таблиця. Той самий клас багу, що вже ловили з vbat і soil_at_ms.

Revision ID: d2f8a41c96b7
Revises: b7d3e91a4c05
Create Date: 2026-09-02 13:20:00.000000

"""
from typing import Sequence, Union

from alembic import op
import sqlalchemy as sa


revision: str = 'd2f8a41c96b7'
down_revision: Union[str, Sequence[str], None] = 'b7d3e91a4c05'
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    op.add_column('measurement', sa.Column('soil_min', sa.Integer(), nullable=True))
    op.add_column('measurement', sa.Column('soil_max', sa.Integer(), nullable=True))
    op.add_column('measurement', sa.Column('soil_n', sa.Integer(), nullable=True))


def downgrade() -> None:
    op.drop_column('measurement', 'soil_n')
    op.drop_column('measurement', 'soil_max')
    op.drop_column('measurement', 'soil_min')

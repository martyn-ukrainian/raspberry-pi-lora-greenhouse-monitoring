"""add soil_at_ms and boot to measurement

Прошивка -lowpower везе обидва поля з коміту 1359c02, але приймач їх не
оголошував, тож pydantic їх мовчки викидав. Та сама історія, що з vbat.

Revision ID: b7d3e91a4c05
Revises: c4f1a8e37b02
Create Date: 2026-08-27 15:10:00.000000

"""
from typing import Sequence, Union

from alembic import op
import sqlalchemy as sa


revision: str = 'b7d3e91a4c05'
down_revision: Union[str, Sequence[str], None] = 'c4f1a8e37b02'
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    op.add_column('measurement', sa.Column('soil_at_ms', sa.Integer(), nullable=True))
    op.add_column('measurement', sa.Column('boot', sa.Integer(), nullable=True))


def downgrade() -> None:
    op.drop_column('measurement', 'boot')
    op.drop_column('measurement', 'soil_at_ms')

-- SQL Server pads the shorter character operand with U+0020 for
-- comparisons other than LIKE and treats NULL as a duplicate value
-- in unique keys. These supplemental indexes keep the original
-- catalog constraints while enforcing those two observable rules.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE UNIQUE INDEX "ux_mssql_key_agit_bid_uq_agit_bid_1"
ON "world"."agit_bid" (
    "auction_id",
    "attend_pledge_id"
)
NULLS NOT DISTINCT;

CREATE UNIQUE INDEX "ux_mssql_key_alliance_pk_alliance"
ON "world"."alliance" (
    (rtrim("name", ' ') COLLATE "world"."Korean_Wansung_CI_AS")
);

CREATE UNIQUE INDEX "ux_mssql_key_builder_account_pk_builder_account"
ON "world"."builder_account" (
    (rtrim("account_name", ' ') COLLATE "world"."Korean_Wansung_CI_AS")
);

CREATE UNIQUE INDEX "ux_mssql_key_control_tower_pk_control_tower"
ON "world"."control_tower" (
    (rtrim("name", ' ') COLLATE "world"."Korean_Wansung_CI_AS")
);

CREATE UNIQUE INDEX "ux_mssql_key_daily_reset_time_pk_daily_reset_time"
ON "world"."daily_reset_time" (
    (rtrim("event_name", ' ') COLLATE "world"."Korean_Wansung_CI_AS")
);

CREATE UNIQUE INDEX "ux_mssql_key_door_pk_door"
ON "world"."door" (
    (rtrim("name", ' ') COLLATE "world"."Korean_Wansung_CI_AS")
);

CREATE UNIQUE INDEX "ux_mssql_key_npc_boss_pk_npc_boss"
ON "world"."npc_boss" (
    (rtrim("npc_db_name", ' ') COLLATE "world"."Korean_Wansung_CI_AS")
);

CREATE UNIQUE INDEX "ux_mssql_key_petiton_external_notify_pk_petiton_external_notify"
ON "world"."petiton_external_notify" (
    "world_id",
    (rtrim("char_name", ' ') COLLATE "world"."Korean_Wansung_CI_AS")
);

CREATE UNIQUE INDEX "ux_mssql_key_pledge_uq_pledge_1"
ON "world"."pledge" (
    (rtrim("name", ' ') COLLATE "world"."Korean_Wansung_CI_AS")
);

CREATE UNIQUE INDEX "ux_mssql_key_post_additional_friend_pk_post_additional_friend"
ON "world"."post_additional_friend" (
    "char_id",
    (rtrim("friend_name", ' ') COLLATE "world"."Korean_Wansung_CI_AS")
);

CREATE UNIQUE INDEX "ux_mssql_key_user_macro_uq_user_macro_1"
ON "world"."user_macro" (
    "macro_id",
    "char_id"
)
NULLS NOT DISTINCT;

CREATE UNIQUE INDEX "ux_mssql_key_user_name_reserved_pk_user_name_reserved"
ON "world"."user_name_reserved" (
    (rtrim("char_name", ' ') COLLATE "world"."Korean_Wansung_CI_AS")
);

CREATE UNIQUE INDEX "ux_mssql_key_user_prohibit_pk_user_prohibit"
ON "world"."user_prohibit" (
    (rtrim("char_name", ' ') COLLATE "world"."Korean_Wansung_CI_AS")
);

CREATE UNIQUE INDEX "ux_mssql_key_user_prohibit_word_pk_user_prohibit_word"
ON "world"."user_prohibit_word" (
    (rtrim("words", ' ') COLLATE "world"."Korean_Wansung_CI_AS")
);

COMMIT;

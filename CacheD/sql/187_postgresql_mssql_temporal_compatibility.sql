-- Applies the SQL Server DATETIME and SMALLDATETIME rules of migration 000
-- to the time columns, converts existing data and keeps the conversion on
-- every write with BEFORE row triggers.

BEGIN;

UPDATE "world"."academy"
SET
    "created_date" = "world"."mssql_datetime"("created_date")
WHERE
    ("created_date" IS NOT NULL AND "created_date" IS DISTINCT FROM "world"."mssql_datetime"("created_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_academy"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."created_date" := "world"."mssql_datetime"(NEW."created_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_academy_temporal"
BEFORE INSERT OR UPDATE OF "created_date" ON "world"."academy"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_academy"();

UPDATE "world"."academy_member"
SET
    "status_time" = "world"."mssql_datetime"("status_time")
WHERE
    ("status_time" IS NOT NULL AND "status_time" IS DISTINCT FROM "world"."mssql_datetime"("status_time"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_academy_member"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."status_time" := "world"."mssql_datetime"(NEW."status_time");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_academy_member_temporal"
BEFORE INSERT OR UPDATE OF "status_time" ON "world"."academy_member"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_academy_member"();

UPDATE "world"."account_user_pccafe_daily_point"
SET
    "last_update_time" = "world"."mssql_datetime"("last_update_time")
WHERE
    ("last_update_time" IS NOT NULL AND "last_update_time" IS DISTINCT FROM "world"."mssql_datetime"("last_update_time"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_account_user_pccafe_daily_point"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."last_update_time" := "world"."mssql_datetime"(NEW."last_update_time");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_account_user_pccafe_daily_point_temporal"
BEFORE INSERT OR UPDATE OF "last_update_time" ON "world"."account_user_pccafe_daily_point"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_account_user_pccafe_daily_point"();

UPDATE "world"."agit_bid"
SET
    "attend_date" = "world"."mssql_datetime"("attend_date")
WHERE
    ("attend_date" IS NOT NULL AND "attend_date" IS DISTINCT FROM "world"."mssql_datetime"("attend_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_agit_bid"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."attend_date" := "world"."mssql_datetime"(NEW."attend_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_agit_bid_temporal"
BEFORE INSERT OR UPDATE OF "attend_date" ON "world"."agit_bid"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_agit_bid"();

UPDATE "world"."br_drop_item_amount"
SET
    "drop_time" = "world"."mssql_datetime"("drop_time")
WHERE
    ("drop_time" IS NOT NULL AND "drop_time" IS DISTINCT FROM "world"."mssql_datetime"("drop_time"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_br_drop_item_amount"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."drop_time" := "world"."mssql_datetime"(NEW."drop_time");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_br_drop_item_amount_temporal"
BEFORE INSERT OR UPDATE OF "drop_time" ON "world"."br_drop_item_amount"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_br_drop_item_amount"();

UPDATE "world"."br_event_round"
SET
    "end_date" = "world"."mssql_datetime"("end_date")
WHERE
    ("end_date" IS NOT NULL AND "end_date" IS DISTINCT FROM "world"."mssql_datetime"("end_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_br_event_round"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."end_date" := "world"."mssql_datetime"(NEW."end_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_br_event_round_temporal"
BEFORE INSERT OR UPDATE OF "end_date" ON "world"."br_event_round"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_br_event_round"();

UPDATE "world"."br_event_score"
SET
    "score_date" = "world"."mssql_datetime"("score_date")
WHERE
    ("score_date" IS NOT NULL AND "score_date" IS DISTINCT FROM "world"."mssql_datetime"("score_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_br_event_score"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."score_date" := "world"."mssql_datetime"(NEW."score_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_br_event_score_temporal"
BEFORE INSERT OR UPDATE OF "score_date" ON "world"."br_event_score"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_br_event_score"();

UPDATE "world"."br_event_top_players"
SET
    "score_date" = "world"."mssql_datetime"("score_date")
WHERE
    ("score_date" IS NOT NULL AND "score_date" IS DISTINCT FROM "world"."mssql_datetime"("score_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_br_event_top_players"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."score_date" := "world"."mssql_datetime"(NEW."score_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_br_event_top_players_temporal"
BEFORE INSERT OR UPDATE OF "score_date" ON "world"."br_event_top_players"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_br_event_top_players"();

UPDATE "world"."br_interested_product"
SET
    "insert_date" = "world"."mssql_datetime"("insert_date")
WHERE
    ("insert_date" IS NOT NULL AND "insert_date" IS DISTINCT FROM "world"."mssql_datetime"("insert_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_br_interested_product"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."insert_date" := "world"."mssql_datetime"(NEW."insert_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_br_interested_product_temporal"
BEFORE INSERT OR UPDATE OF "insert_date" ON "world"."br_interested_product"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_br_interested_product"();

UPDATE "world"."br_minigame_score"
SET
    "score_date" = "world"."mssql_datetime"("score_date")
WHERE
    ("score_date" IS NOT NULL AND "score_date" IS DISTINCT FROM "world"."mssql_datetime"("score_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_br_minigame_score"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."score_date" := "world"."mssql_datetime"(NEW."score_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_br_minigame_score_temporal"
BEFORE INSERT OR UPDATE OF "score_date" ON "world"."br_minigame_score"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_br_minigame_score"();

UPDATE "world"."br_recent_product"
SET
    "buy_date" = "world"."mssql_datetime"("buy_date")
WHERE
    ("buy_date" IS NOT NULL AND "buy_date" IS DISTINCT FROM "world"."mssql_datetime"("buy_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_br_recent_product"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."buy_date" := "world"."mssql_datetime"(NEW."buy_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_br_recent_product_temporal"
BEFORE INSERT OR UPDATE OF "buy_date" ON "world"."br_recent_product"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_br_recent_product"();

UPDATE "world"."br_user_event_data"
SET
    "event_time" = "world"."mssql_datetime"("event_time")
WHERE
    ("event_time" IS NOT NULL AND "event_time" IS DISTINCT FROM "world"."mssql_datetime"("event_time"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_br_user_event_data"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."event_time" := "world"."mssql_datetime"(NEW."event_time");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_br_user_event_data_temporal"
BEFORE INSERT OR UPDATE OF "event_time" ON "world"."br_user_event_data"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_br_user_event_data"();

UPDATE "world"."br_user_shopitem_limit"
SET
    "buy_date_log" = "world"."mssql_datetime"("buy_date_log")
WHERE
    ("buy_date_log" IS NOT NULL AND "buy_date_log" IS DISTINCT FROM "world"."mssql_datetime"("buy_date_log"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_br_user_shopitem_limit"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."buy_date_log" := "world"."mssql_datetime"(NEW."buy_date_log");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_br_user_shopitem_limit_temporal"
BEFORE INSERT OR UPDATE OF "buy_date_log" ON "world"."br_user_shopitem_limit"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_br_user_shopitem_limit"();

UPDATE "world"."castle"
SET
    "load_date" = "world"."mssql_datetime"("load_date"),
    "unload_date" = "world"."mssql_datetime"("unload_date")
WHERE
    ("load_date" IS NOT NULL AND "load_date" IS DISTINCT FROM "world"."mssql_datetime"("load_date"))
    OR ("unload_date" IS NOT NULL AND "unload_date" IS DISTINCT FROM "world"."mssql_datetime"("unload_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_castle"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."load_date" := "world"."mssql_datetime"(NEW."load_date");
    NEW."unload_date" := "world"."mssql_datetime"(NEW."unload_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_castle_temporal"
BEFORE INSERT OR UPDATE OF "load_date", "unload_date" ON "world"."castle"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_castle"();

UPDATE "world"."castle_tax"
SET
    "income_update" = "world"."mssql_datetime"("income_update"),
    "manor_reset" = "world"."mssql_datetime"("manor_reset"),
    "tax_change" = "world"."mssql_datetime"("tax_change")
WHERE
    ("income_update" IS NOT NULL AND "income_update" IS DISTINCT FROM "world"."mssql_datetime"("income_update"))
    OR ("manor_reset" IS NOT NULL AND "manor_reset" IS DISTINCT FROM "world"."mssql_datetime"("manor_reset"))
    OR ("tax_change" IS NOT NULL AND "tax_change" IS DISTINCT FROM "world"."mssql_datetime"("tax_change"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_castle_tax"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."income_update" := "world"."mssql_datetime"(NEW."income_update");
    NEW."manor_reset" := "world"."mssql_datetime"(NEW."manor_reset");
    NEW."tax_change" := "world"."mssql_datetime"(NEW."tax_change");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_castle_tax_temporal"
BEFORE INSERT OR UPDATE OF "income_update", "manor_reset", "tax_change" ON "world"."castle_tax"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_castle_tax"();

UPDATE "world"."changed_name_by_merge"
SET
    "change_date" = "world"."mssql_datetime"("change_date")
WHERE
    ("change_date" IS NOT NULL AND "change_date" IS DISTINCT FROM "world"."mssql_datetime"("change_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_changed_name_by_merge"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."change_date" := "world"."mssql_datetime"(NEW."change_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_changed_name_by_merge_temporal"
BEFORE INSERT OR UPDATE OF "change_date" ON "world"."changed_name_by_merge"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_changed_name_by_merge"();

UPDATE "world"."commission"
SET
    "item_delete_time" = "world"."mssql_datetime"("item_delete_time"),
    "item_register_time" = "world"."mssql_datetime"("item_register_time")
WHERE
    ("item_delete_time" IS NOT NULL AND "item_delete_time" IS DISTINCT FROM "world"."mssql_datetime"("item_delete_time"))
    OR ("item_register_time" IS NOT NULL AND "item_register_time" IS DISTINCT FROM "world"."mssql_datetime"("item_register_time"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_commission"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."item_delete_time" := "world"."mssql_datetime"(NEW."item_delete_time");
    NEW."item_register_time" := "world"."mssql_datetime"(NEW."item_register_time");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_commission_temporal"
BEFORE INSERT OR UPDATE OF "item_delete_time", "item_register_time" ON "world"."commission"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_commission"();

UPDATE "world"."commission_abnormal"
SET
    "item_delete_time" = "world"."mssql_datetime"("item_delete_time"),
    "item_register_time" = "world"."mssql_datetime"("item_register_time")
WHERE
    ("item_delete_time" IS NOT NULL AND "item_delete_time" IS DISTINCT FROM "world"."mssql_datetime"("item_delete_time"))
    OR ("item_register_time" IS NOT NULL AND "item_register_time" IS DISTINCT FROM "world"."mssql_datetime"("item_register_time"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_commission_abnormal"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."item_delete_time" := "world"."mssql_datetime"(NEW."item_delete_time");
    NEW."item_register_time" := "world"."mssql_datetime"(NEW."item_register_time");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_commission_abnormal_temporal"
BEFORE INSERT OR UPDATE OF "item_delete_time", "item_register_time" ON "world"."commission_abnormal"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_commission_abnormal"();

UPDATE "world"."commission_history"
SET
    "item_delete_time" = "world"."mssql_datetime"("item_delete_time"),
    "item_register_time" = "world"."mssql_datetime"("item_register_time")
WHERE
    ("item_delete_time" IS NOT NULL AND "item_delete_time" IS DISTINCT FROM "world"."mssql_datetime"("item_delete_time"))
    OR ("item_register_time" IS NOT NULL AND "item_register_time" IS DISTINCT FROM "world"."mssql_datetime"("item_register_time"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_commission_history"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."item_delete_time" := "world"."mssql_datetime"(NEW."item_delete_time");
    NEW."item_register_time" := "world"."mssql_datetime"(NEW."item_register_time");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_commission_history_temporal"
BEFORE INSERT OR UPDATE OF "item_delete_time", "item_register_time" ON "world"."commission_history"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_commission_history"();

UPDATE "world"."eventdata"
SET
    "create_date" = "world"."mssql_datetime"("create_date")
WHERE
    ("create_date" IS NOT NULL AND "create_date" IS DISTINCT FROM "world"."mssql_datetime"("create_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_eventdata"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."create_date" := "world"."mssql_datetime"(NEW."create_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_eventdata_temporal"
BEFORE INSERT OR UPDATE OF "create_date" ON "world"."eventdata"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_eventdata"();

UPDATE "world"."eventitemfromweb"
SET
    "apply_date" = "world"."mssql_datetime"("apply_date")
WHERE
    ("apply_date" IS NOT NULL AND "apply_date" IS DISTINCT FROM "world"."mssql_datetime"("apply_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_eventitemfromweb"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."apply_date" := "world"."mssql_datetime"(NEW."apply_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_eventitemfromweb_temporal"
BEFORE INSERT OR UPDATE OF "apply_date" ON "world"."eventitemfromweb"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_eventitemfromweb"();

UPDATE "world"."item_auction_bid"
SET
    "bidding_date" = "world"."mssql_datetime"("bidding_date")
WHERE
    ("bidding_date" IS NOT NULL AND "bidding_date" IS DISTINCT FROM "world"."mssql_datetime"("bidding_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_item_auction_bid"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."bidding_date" := "world"."mssql_datetime"(NEW."bidding_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_item_auction_bid_temporal"
BEFORE INSERT OR UPDATE OF "bidding_date" ON "world"."item_auction_bid"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_item_auction_bid"();

UPDATE "world"."manor_info"
SET
    "last_changed" = "world"."mssql_datetime"("last_changed")
WHERE
    ("last_changed" IS NOT NULL AND "last_changed" IS DISTINCT FROM "world"."mssql_datetime"("last_changed"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_manor_info"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."last_changed" := "world"."mssql_datetime"(NEW."last_changed");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_manor_info_temporal"
BEFORE INSERT OR UPDATE OF "last_changed" ON "world"."manor_info"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_manor_info"();

UPDATE "world"."minigame_agit_member"
SET
    "propose_time" = "world"."mssql_datetime"("propose_time")
WHERE
    ("propose_time" IS NOT NULL AND "propose_time" IS DISTINCT FROM "world"."mssql_datetime"("propose_time"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_minigame_agit_member"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."propose_time" := "world"."mssql_datetime"(NEW."propose_time");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_minigame_agit_member_temporal"
BEFORE INSERT OR UPDATE OF "propose_time" ON "world"."minigame_agit_member"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_minigame_agit_member"();

UPDATE "world"."minigame_agit_pledge"
SET
    "propose_time" = "world"."mssql_datetime"("propose_time")
WHERE
    ("propose_time" IS NOT NULL AND "propose_time" IS DISTINCT FROM "world"."mssql_datetime"("propose_time"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_minigame_agit_pledge"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."propose_time" := "world"."mssql_datetime"(NEW."propose_time");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_minigame_agit_pledge_temporal"
BEFORE INSERT OR UPDATE OF "propose_time" ON "world"."minigame_agit_pledge"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_minigame_agit_pledge"();

UPDATE "world"."minigame_agit_status"
SET
    "propose_time" = "world"."mssql_datetime"("propose_time")
WHERE
    ("propose_time" IS NOT NULL AND "propose_time" IS DISTINCT FROM "world"."mssql_datetime"("propose_time"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_minigame_agit_status"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."propose_time" := "world"."mssql_datetime"(NEW."propose_time");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_minigame_agit_status_temporal"
BEFORE INSERT OR UPDATE OF "propose_time" ON "world"."minigame_agit_status"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_minigame_agit_status"();

UPDATE "world"."monrace"
SET
    "make_date" = "world"."mssql_datetime"("make_date")
WHERE
    ("make_date" IS NOT NULL AND "make_date" IS DISTINCT FROM "world"."mssql_datetime"("make_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_monrace"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."make_date" := "world"."mssql_datetime"(NEW."make_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_monrace_temporal"
BEFORE INSERT OR UPDATE OF "make_date" ON "world"."monrace"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_monrace"();

UPDATE "world"."npc_boss"
SET
    "died_time" = "world"."mssql_datetime"("died_time"),
    "generated_time" = "world"."mssql_datetime"("generated_time")
WHERE
    ("died_time" IS NOT NULL AND "died_time" IS DISTINCT FROM "world"."mssql_datetime"("died_time"))
    OR ("generated_time" IS NOT NULL AND "generated_time" IS DISTINCT FROM "world"."mssql_datetime"("generated_time"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_npc_boss"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."died_time" := "world"."mssql_datetime"(NEW."died_time");
    NEW."generated_time" := "world"."mssql_datetime"(NEW."generated_time");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_npc_boss_temporal"
BEFORE INSERT OR UPDATE OF "died_time", "generated_time" ON "world"."npc_boss"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_npc_boss"();

UPDATE "world"."pledge"
SET
    "load_date" = "world"."mssql_datetime"("load_date"),
    "unload_date" = "world"."mssql_datetime"("unload_date")
WHERE
    ("load_date" IS NOT NULL AND "load_date" IS DISTINCT FROM "world"."mssql_datetime"("load_date"))
    OR ("unload_date" IS NOT NULL AND "unload_date" IS DISTINCT FROM "world"."mssql_datetime"("unload_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_pledge"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."load_date" := "world"."mssql_datetime"(NEW."load_date");
    NEW."unload_date" := "world"."mssql_datetime"(NEW."unload_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_pledge_temporal"
BEFORE INSERT OR UPDATE OF "load_date", "unload_date" ON "world"."pledge"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_pledge"();

UPDATE "world"."pledge_master_transfer"
SET
    "status_time" = "world"."mssql_datetime"("status_time")
WHERE
    ("status_time" IS NOT NULL AND "status_time" IS DISTINCT FROM "world"."mssql_datetime"("status_time"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_pledge_master_transfer"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."status_time" := "world"."mssql_datetime"(NEW."status_time");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_pledge_master_transfer_temporal"
BEFORE INSERT OR UPDATE OF "status_time" ON "world"."pledge_master_transfer"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_pledge_master_transfer"();

UPDATE "world"."pledge_namevalue_log"
SET
    "log_time" = "world"."mssql_datetime"("log_time")
WHERE
    ("log_time" IS NOT NULL AND "log_time" IS DISTINCT FROM "world"."mssql_datetime"("log_time"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_pledge_namevalue_log"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."log_time" := "world"."mssql_datetime"(NEW."log_time");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_pledge_namevalue_log_temporal"
BEFORE INSERT OR UPDATE OF "log_time" ON "world"."pledge_namevalue_log"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_pledge_namevalue_log"();

UPDATE "world"."premium_kamaloka_record"
SET
    "saved_time" = "world"."mssql_datetime"("saved_time")
WHERE
    ("saved_time" IS NOT NULL AND "saved_time" IS DISTINCT FROM "world"."mssql_datetime"("saved_time"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_premium_kamaloka_record"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."saved_time" := "world"."mssql_datetime"(NEW."saved_time");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_premium_kamaloka_record_temporal"
BEFORE INSERT OR UPDATE OF "saved_time" ON "world"."premium_kamaloka_record"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_premium_kamaloka_record"();

UPDATE "world"."questdata"
SET
    "logdate" = "world"."mssql_smalldatetime"("logdate")
WHERE
    ("logdate" IS NOT NULL AND "logdate" IS DISTINCT FROM "world"."mssql_smalldatetime"("logdate"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_questdata"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."logdate" := "world"."mssql_smalldatetime"(NEW."logdate");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_questdata_temporal"
BEFORE INSERT OR UPDATE OF "logdate" ON "world"."questdata"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_questdata"();

UPDATE "world"."regular_db_job"
SET
    "execute_time" = "world"."mssql_datetime"("execute_time")
WHERE
    ("execute_time" IS NOT NULL AND "execute_time" IS DISTINCT FROM "world"."mssql_datetime"("execute_time"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_regular_db_job"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."execute_time" := "world"."mssql_datetime"(NEW."execute_time");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_regular_db_job_temporal"
BEFORE INSERT OR UPDATE OF "execute_time" ON "world"."regular_db_job"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_regular_db_job"();

UPDATE "world"."skilldata"
SET
    "logdate" = "world"."mssql_smalldatetime"("logdate")
WHERE
    ("logdate" IS NOT NULL AND "logdate" IS DISTINCT FROM "world"."mssql_smalldatetime"("logdate"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_skilldata"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."logdate" := "world"."mssql_smalldatetime"(NEW."logdate");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_skilldata_temporal"
BEFORE INSERT OR UPDATE OF "logdate" ON "world"."skilldata"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_skilldata"();

UPDATE "world"."ssq_data"
SET
    "last_changed_time" = "world"."mssql_datetime"("last_changed_time")
WHERE
    ("last_changed_time" IS NOT NULL AND "last_changed_time" IS DISTINCT FROM "world"."mssql_datetime"("last_changed_time"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_ssq_data"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."last_changed_time" := "world"."mssql_datetime"(NEW."last_changed_time");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_ssq_data_temporal"
BEFORE INSERT OR UPDATE OF "last_changed_time" ON "world"."ssq_data"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_ssq_data"();

UPDATE "world"."ssq_join_data"
SET
    "last_changed_time" = "world"."mssql_datetime"("last_changed_time")
WHERE
    ("last_changed_time" IS NOT NULL AND "last_changed_time" IS DISTINCT FROM "world"."mssql_datetime"("last_changed_time"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_ssq_join_data"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."last_changed_time" := "world"."mssql_datetime"(NEW."last_changed_time");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_ssq_join_data_temporal"
BEFORE INSERT OR UPDATE OF "last_changed_time" ON "world"."ssq_join_data"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_ssq_join_data"();

UPDATE "world"."time_data"
SET
    "last_logout" = "world"."mssql_datetime"("last_logout")
WHERE
    ("last_logout" IS NOT NULL AND "last_logout" IS DISTINCT FROM "world"."mssql_datetime"("last_logout"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_time_data"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."last_logout" := "world"."mssql_datetime"(NEW."last_logout");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_time_data_temporal"
BEFORE INSERT OR UPDATE OF "last_logout" ON "world"."time_data"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_time_data"();

UPDATE "world"."user_ban"
SET
    "ban_date" = "world"."mssql_datetime"("ban_date")
WHERE
    ("ban_date" IS NOT NULL AND "ban_date" IS DISTINCT FROM "world"."mssql_datetime"("ban_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_user_ban"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."ban_date" := "world"."mssql_datetime"(NEW."ban_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_user_ban_temporal"
BEFORE INSERT OR UPDATE OF "ban_date" ON "world"."user_ban"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_user_ban"();

UPDATE "world"."user_birthday_history"
SET
    "get_date" = "world"."mssql_datetime"("get_date")
WHERE
    ("get_date" IS NOT NULL AND "get_date" IS DISTINCT FROM "world"."mssql_datetime"("get_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_user_birthday_history"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."get_date" := "world"."mssql_datetime"(NEW."get_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_user_birthday_history_temporal"
BEFORE INSERT OR UPDATE OF "get_date" ON "world"."user_birthday_history"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_user_birthday_history"();

UPDATE "world"."user_blocklist"
SET
    "date" = "world"."mssql_datetime"("date")
WHERE
    ("date" IS NOT NULL AND "date" IS DISTINCT FROM "world"."mssql_datetime"("date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_user_blocklist"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."date" := "world"."mssql_datetime"(NEW."date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_user_blocklist_temporal"
BEFORE INSERT OR UPDATE OF "date" ON "world"."user_blocklist"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_user_blocklist"();

UPDATE "world"."user_comment"
SET
    "comment_date" = "world"."mssql_datetime"("comment_date")
WHERE
    ("comment_date" IS NOT NULL AND "comment_date" IS DISTINCT FROM "world"."mssql_datetime"("comment_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_user_comment"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."comment_date" := "world"."mssql_datetime"(NEW."comment_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_user_comment_temporal"
BEFORE INSERT OR UPDATE OF "comment_date" ON "world"."user_comment"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_user_comment"();

UPDATE "world"."user_daily_count"
SET
    "last_update_time" = "world"."mssql_datetime"("last_update_time")
WHERE
    ("last_update_time" IS NOT NULL AND "last_update_time" IS DISTINCT FROM "world"."mssql_datetime"("last_update_time"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_user_daily_count"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."last_update_time" := "world"."mssql_datetime"(NEW."last_update_time");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_user_daily_count_temporal"
BEFORE INSERT OR UPDATE OF "last_update_time" ON "world"."user_daily_count"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_user_daily_count"();

UPDATE "world"."user_data"
SET
    "create_date" = "world"."mssql_datetime"("create_date"),
    "load_date" = "world"."mssql_datetime"("load_date"),
    "login" = "world"."mssql_datetime"("login"),
    "logout" = "world"."mssql_datetime"("logout"),
    "temp_delete_date" = "world"."mssql_smalldatetime"("temp_delete_date"),
    "unload_date" = "world"."mssql_datetime"("unload_date")
WHERE
    ("create_date" IS NOT NULL AND "create_date" IS DISTINCT FROM "world"."mssql_datetime"("create_date"))
    OR ("load_date" IS NOT NULL AND "load_date" IS DISTINCT FROM "world"."mssql_datetime"("load_date"))
    OR ("login" IS NOT NULL AND "login" IS DISTINCT FROM "world"."mssql_datetime"("login"))
    OR ("logout" IS NOT NULL AND "logout" IS DISTINCT FROM "world"."mssql_datetime"("logout"))
    OR ("temp_delete_date" IS NOT NULL AND "temp_delete_date" IS DISTINCT FROM "world"."mssql_smalldatetime"("temp_delete_date"))
    OR ("unload_date" IS NOT NULL AND "unload_date" IS DISTINCT FROM "world"."mssql_datetime"("unload_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_user_data"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."create_date" := "world"."mssql_datetime"(NEW."create_date");
    NEW."load_date" := "world"."mssql_datetime"(NEW."load_date");
    NEW."login" := "world"."mssql_datetime"(NEW."login");
    NEW."logout" := "world"."mssql_datetime"(NEW."logout");
    NEW."temp_delete_date" := "world"."mssql_smalldatetime"(NEW."temp_delete_date");
    NEW."unload_date" := "world"."mssql_datetime"(NEW."unload_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_user_data_temporal"
BEFORE INSERT OR UPDATE OF "create_date", "load_date", "login", "logout", "temp_delete_date", "unload_date" ON "world"."user_data"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_user_data"();

UPDATE "world"."user_data_extra"
SET
    "modified_date" = "world"."mssql_datetime"("modified_date")
WHERE
    ("modified_date" IS NOT NULL AND "modified_date" IS DISTINCT FROM "world"."mssql_datetime"("modified_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_user_data_extra"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."modified_date" := "world"."mssql_datetime"(NEW."modified_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_user_data_extra_temporal"
BEFORE INSERT OR UPDATE OF "modified_date" ON "world"."user_data_extra"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_user_data_extra"();

UPDATE "world"."user_deleted"
SET
    "delete_date" = "world"."mssql_datetime"("delete_date")
WHERE
    ("delete_date" IS NOT NULL AND "delete_date" IS DISTINCT FROM "world"."mssql_datetime"("delete_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_user_deleted"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."delete_date" := "world"."mssql_datetime"(NEW."delete_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_user_deleted_temporal"
BEFORE INSERT OR UPDATE OF "delete_date" ON "world"."user_deleted"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_user_deleted"();

UPDATE "world"."user_dormant"
SET
    "gift_received_date" = "world"."mssql_datetime"("gift_received_date")
WHERE
    ("gift_received_date" IS NOT NULL AND "gift_received_date" IS DISTINCT FROM "world"."mssql_datetime"("gift_received_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_user_dormant"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."gift_received_date" := "world"."mssql_datetime"(NEW."gift_received_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_user_dormant_temporal"
BEFORE INSERT OR UPDATE OF "gift_received_date" ON "world"."user_dormant"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_user_dormant"();

UPDATE "world"."user_history"
SET
    "create_date" = "world"."mssql_datetime"("create_date"),
    "log_date" = "world"."mssql_datetime"("log_date")
WHERE
    ("create_date" IS NOT NULL AND "create_date" IS DISTINCT FROM "world"."mssql_datetime"("create_date"))
    OR ("log_date" IS NOT NULL AND "log_date" IS DISTINCT FROM "world"."mssql_datetime"("log_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_user_history"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."create_date" := "world"."mssql_datetime"(NEW."create_date");
    NEW."log_date" := "world"."mssql_datetime"(NEW."log_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_user_history_temporal"
BEFORE INSERT OR UPDATE OF "create_date", "log_date" ON "world"."user_history"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_user_history"();

UPDATE "world"."user_log"
SET
    "log_date" = "world"."mssql_datetime"("log_date")
WHERE
    ("log_date" IS NOT NULL AND "log_date" IS DISTINCT FROM "world"."mssql_datetime"("log_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_user_log"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."log_date" := "world"."mssql_datetime"(NEW."log_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_user_log_temporal"
BEFORE INSERT OR UPDATE OF "log_date" ON "world"."user_log"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_user_log"();

UPDATE "world"."user_mail"
SET
    "created_date" = "world"."mssql_datetime"("created_date")
WHERE
    ("created_date" IS NOT NULL AND "created_date" IS DISTINCT FROM "world"."mssql_datetime"("created_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_user_mail"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."created_date" := "world"."mssql_datetime"(NEW."created_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_user_mail_temporal"
BEFORE INSERT OR UPDATE OF "created_date" ON "world"."user_mail"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_user_mail"();

UPDATE "world"."user_mail_receiver"
SET
    "read_date" = "world"."mssql_datetime"("read_date")
WHERE
    ("read_date" IS NOT NULL AND "read_date" IS DISTINCT FROM "world"."mssql_datetime"("read_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_user_mail_receiver"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."read_date" := "world"."mssql_datetime"(NEW."read_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_user_mail_receiver_temporal"
BEFORE INSERT OR UPDATE OF "read_date" ON "world"."user_mail_receiver"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_user_mail_receiver"();

UPDATE "world"."user_mail_sender"
SET
    "send_date" = "world"."mssql_datetime"("send_date")
WHERE
    ("send_date" IS NOT NULL AND "send_date" IS DISTINCT FROM "world"."mssql_datetime"("send_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_user_mail_sender"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."send_date" := "world"."mssql_datetime"(NEW."send_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_user_mail_sender_temporal"
BEFORE INSERT OR UPDATE OF "send_date" ON "world"."user_mail_sender"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_user_mail_sender"();

UPDATE "world"."user_premium_item"
SET
    "ibserver_delete_date" = "world"."mssql_datetime"("ibserver_delete_date"),
    "server_receive_date" = "world"."mssql_datetime"("server_receive_date")
WHERE
    ("ibserver_delete_date" IS NOT NULL AND "ibserver_delete_date" IS DISTINCT FROM "world"."mssql_datetime"("ibserver_delete_date"))
    OR ("server_receive_date" IS NOT NULL AND "server_receive_date" IS DISTINCT FROM "world"."mssql_datetime"("server_receive_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_user_premium_item"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."ibserver_delete_date" := "world"."mssql_datetime"(NEW."ibserver_delete_date");
    NEW."server_receive_date" := "world"."mssql_datetime"(NEW."server_receive_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_user_premium_item_temporal"
BEFORE INSERT OR UPDATE OF "ibserver_delete_date", "server_receive_date" ON "world"."user_premium_item"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_user_premium_item"();

UPDATE "world"."user_premium_item_log"
SET
    "create_date" = "world"."mssql_datetime"("create_date"),
    "delete_date" = "world"."mssql_datetime"("delete_date")
WHERE
    ("create_date" IS NOT NULL AND "create_date" IS DISTINCT FROM "world"."mssql_datetime"("create_date"))
    OR ("delete_date" IS NOT NULL AND "delete_date" IS DISTINCT FROM "world"."mssql_datetime"("delete_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_user_premium_item_log"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."create_date" := "world"."mssql_datetime"(NEW."create_date");
    NEW."delete_date" := "world"."mssql_datetime"(NEW."delete_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_user_premium_item_log_temporal"
BEFORE INSERT OR UPDATE OF "create_date", "delete_date" ON "world"."user_premium_item_log"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_user_premium_item_log"();

UPDATE "world"."user_premium_item_receive"
SET
    "receive_date" = "world"."mssql_datetime"("receive_date")
WHERE
    ("receive_date" IS NOT NULL AND "receive_date" IS DISTINCT FROM "world"."mssql_datetime"("receive_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_user_premium_item_receive"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."receive_date" := "world"."mssql_datetime"(NEW."receive_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_user_premium_item_receive_temporal"
BEFORE INSERT OR UPDATE OF "receive_date" ON "world"."user_premium_item_receive"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_user_premium_item_receive"();

UPDATE "world"."user_punish"
SET
    "punish_date" = "world"."mssql_datetime"("punish_date")
WHERE
    ("punish_date" IS NOT NULL AND "punish_date" IS DISTINCT FROM "world"."mssql_datetime"("punish_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_user_punish"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."punish_date" := "world"."mssql_datetime"(NEW."punish_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_user_punish_temporal"
BEFORE INSERT OR UPDATE OF "punish_date" ON "world"."user_punish"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_user_punish"();

UPDATE "world"."user_service"
SET
    "reg_date" = "world"."mssql_datetime"("reg_date")
WHERE
    ("reg_date" IS NOT NULL AND "reg_date" IS DISTINCT FROM "world"."mssql_datetime"("reg_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_user_service"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."reg_date" := "world"."mssql_datetime"(NEW."reg_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_user_service_temporal"
BEFORE INSERT OR UPDATE OF "reg_date" ON "world"."user_service"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_user_service"();

UPDATE "world"."user_sociality"
SET
    "last_changed" = "world"."mssql_datetime"("last_changed")
WHERE
    ("last_changed" IS NOT NULL AND "last_changed" IS DISTINCT FROM "world"."mssql_datetime"("last_changed"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_user_sociality"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."last_changed" := "world"."mssql_datetime"(NEW."last_changed");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_user_sociality_temporal"
BEFORE INSERT OR UPDATE OF "last_changed" ON "world"."user_sociality"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_user_sociality"();

UPDATE "world"."user_subjob"
SET
    "create_date" = "world"."mssql_datetime"("create_date")
WHERE
    ("create_date" IS NOT NULL AND "create_date" IS DISTINCT FROM "world"."mssql_datetime"("create_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_user_subjob"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."create_date" := "world"."mssql_datetime"(NEW."create_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_user_subjob_temporal"
BEFORE INSERT OR UPDATE OF "create_date" ON "world"."user_subjob"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_user_subjob"();

UPDATE "world"."user_unblocklist"
SET
    "date" = "world"."mssql_datetime"("date")
WHERE
    ("date" IS NOT NULL AND "date" IS DISTINCT FROM "world"."mssql_datetime"("date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_user_unblocklist"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."date" := "world"."mssql_datetime"(NEW."date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_user_unblocklist_temporal"
BEFORE INSERT OR UPDATE OF "date" ON "world"."user_unblocklist"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_user_unblocklist"();

UPDATE "world"."user_vote_system"
SET
    "lastinittime" = "world"."mssql_datetime"("lastinittime")
WHERE
    ("lastinittime" IS NOT NULL AND "lastinittime" IS DISTINCT FROM "world"."mssql_datetime"("lastinittime"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_user_vote_system"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."lastinittime" := "world"."mssql_datetime"(NEW."lastinittime");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_user_vote_system_temporal"
BEFORE INSERT OR UPDATE OF "lastinittime" ON "world"."user_vote_system"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_user_vote_system"();

UPDATE "world"."xmas_wish"
SET
    "write_date" = "world"."mssql_datetime"("write_date")
WHERE
    ("write_date" IS NOT NULL AND "write_date" IS DISTINCT FROM "world"."mssql_datetime"("write_date"));

CREATE OR REPLACE FUNCTION "world"."compat_temporal_xmas_wish"()
RETURNS trigger
LANGUAGE plpgsql
AS $function$
BEGIN
    NEW."write_date" := "world"."mssql_datetime"(NEW."write_date");
    RETURN NEW;
END;
$function$;

CREATE TRIGGER "trg_xmas_wish_temporal"
BEFORE INSERT OR UPDATE OF "write_date" ON "world"."xmas_wish"
FOR EACH ROW
EXECUTE FUNCTION "world"."compat_temporal_xmas_wish"();

COMMIT;

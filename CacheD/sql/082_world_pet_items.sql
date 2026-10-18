-- Current Cached.exe opcode 120 / CPet::LoadItems pet-inventory projection.
--
-- Current machine code binds exactly 21 result columns. Classic 3.0 and the
-- protocol-287/Ertheia donors agree on the first 19 columns and relational
-- joins. Current CItem construction and the final two short binds add the
-- hLocked/use-count tail. SQL order is independent of CItem memory order.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadpetitems"(
    p_pet_id integer
) RETURNS TABLE (
    item_id integer,
    item_type integer,
    amount bigint,
    enchant integer,
    eroded integer,
    bless integer,
    ident integer,
    wished integer,
    variation_opt1 integer,
    variation_opt2 integer,
    appearance_type integer,
    attack_attribute_type smallint,
    attack_attribute_value smallint,
    defend_attribute_0 smallint,
    defend_attribute_1 smallint,
    defend_attribute_2 smallint,
    defend_attribute_3 smallint,
    defend_attribute_4 smallint,
    defend_attribute_5 smallint,
    hlocked smallint,
    use_count smallint
)
LANGUAGE sql
STABLE
AS $function$
    SELECT item.item_id,
           item.item_type,
           item.amount,
           item.enchant,
           item.eroded,
           item.bless::integer,
           item.ident,
           item.wished::integer,
           COALESCE(item.variation_opt1, 0),
           COALESCE(item.variation_opt2, 0),
           COALESCE(appearance.apperance_type, 0),
           COALESCE(attribute.attack_attribute_type, -2)::smallint,
           COALESCE(attribute.attack_attribute_value, 0)::smallint,
           COALESCE(attribute.defend_attribute_0, 0)::smallint,
           COALESCE(attribute.defend_attribute_1, 0)::smallint,
           COALESCE(attribute.defend_attribute_2, 0)::smallint,
           COALESCE(attribute.defend_attribute_3, 0)::smallint,
           COALESCE(attribute.defend_attribute_4, 0)::smallint,
           COALESCE(attribute.defend_attribute_5, 0)::smallint,
           item.hlocked,
           item.use_count
    FROM "world"."user_item" AS item
    LEFT JOIN "world"."user_item_attribute" AS attribute
      ON attribute.item_id = item.item_id
    LEFT JOIN "world"."user_item_apperance" AS appearance
      ON appearance.item_id = item.item_id
    WHERE item.char_id = p_pet_id
      AND item.warehouse = 5;
$function$;

COMMIT;
